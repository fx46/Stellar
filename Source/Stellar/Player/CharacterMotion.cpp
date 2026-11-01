#include "Player/CharacterMotion.h"

namespace
{
	FVector3d Turn(const FVector3d& V, const FVector3d& Axis, double Angle)
	{
		const double C = FMath::Cos(Angle), S = FMath::Sin(Angle);
		return V * C + Axis.Cross(V) * S + Axis * (Axis.Dot(V) * (1 - C));
	}

	void Rotation(FVector3d From, FVector3d To, FVector3d& OutAxis, double& OutAngle)
	{
		From = From.GetSafeNormal();
		To = To.GetSafeNormal();
		const FVector3d Axis = From.Cross(To);
		const double Length = Axis.Length(), Dot = FMath::Clamp(From.Dot(To), -1.0, 1.0);
		if (Length < 1e-9)
		{
			if (Dot >= 0)
			{
				OutAxis = FVector3d(0, 0, 1);
				OutAngle = 0;
			}
			else
			{
				OutAxis = From.Cross(FMath::Abs(From.Z) < 0.9 ? FVector3d(0, 0, 1) : FVector3d(1, 0, 0)).GetSafeNormal();
				OutAngle = UE_DOUBLE_PI;
			}
			return;
		}
		OutAxis = Axis / Length;
		OutAngle = FMath::Atan2(Length, Dot);
	}

	FVector3d Lerp(const FVector3d& A, const FVector3d& B, double T) { return A + (B - A) * T; }
}

FVector3d PlayerOrientation::SmoothUp(const FVector3d& Current, const FVector3d& Target, double Rate, double Dt)
{
	if (Target.SizeSquared() < 1e-12) return Current;
	if (Current.SizeSquared() < 1e-12) return Target.GetSafeNormal();
	FVector3d Axis;
	double Angle;
	Rotation(Current, Target, Axis, Angle);
	const double Step = FMath::Min(Angle * (1 - FMath::Exp(-Rate * Dt)), MaxUpRadiansPerSecond * Dt);
	return Turn(Current.GetSafeNormal(), Axis, Step).GetSafeNormal();
}

FVector3d PlayerOrientation::Transport(const FVector3d& Heading, const FVector3d& OldUp, const FVector3d& NewUp)
{
	if (OldUp.SizeSquared() < 1e-12 || NewUp.SizeSquared() < 1e-12) return Heading;
	FVector3d Axis;
	double Angle;
	Rotation(OldUp, NewUp, Axis, Angle);
	return Turn(Heading, Axis, Angle);
}

void FJumpJetInput::Reset()
{
	bSpaceWasHeld = bJetArmed = false;
	SpaceHeldTime = JumpBuffer = Coyote = 0;
}

void FJumpJetInput::Update(bool bSpace, bool bGrounded, bool bCanThrust, double Dt, bool& bOutJump, bool& bOutJet)
{
	const bool bPressed = bSpace && !bSpaceWasHeld;
	bSpaceWasHeld = bSpace;
	const bool bCanJump = bGrounded || Coyote > 0;
	if (bPressed)
	{
		JumpBuffer = JumpBufferTime;
		// A fresh press while already airborne (and past coyote time) means "fly".
		if (!bCanJump)
			bJetArmed = true;
	}
	SpaceHeldTime = bSpace ? SpaceHeldTime + Dt : 0;
	if (!bSpace)
		bJetArmed = false;

	const bool bJump = JumpBuffer > 0 && bCanJump;
	JumpBuffer -= Dt;
	if (bJump)
	{
		JumpBuffer = 0;
		Coyote = 0;
		SpaceHeldTime = 0;
	}
	// Holding on after a jump lights the pack once the hop has had its moment.
	if (bSpace && !bJump && SpaceHeldTime >= JetHoldDelay)
		bJetArmed = true;
	bOutJump = bJump;
	bOutJet = bJetArmed && !bJump && !bGrounded && bCanThrust;
}

void FJumpJetInput::AfterStep(bool bGrounded, double Dt)
{
	if (bGrounded)
		Coyote = CoyoteTime;
	else
		Coyote -= Dt;
}

void FSuit::Tick(double Dt, ESuitActivity Activity)
{
	if (bGodMode)
	{
		Power = MaxPower;
		return;
	}
	double Rate = -TrickleRate;
	switch (Activity)
	{
	case ESuitActivity::Jetpack: Rate = JetpackDrain; break;
	case ESuitActivity::Hovering: Rate = HoverDrain; break;
	case ESuitActivity::Walking: Rate = WalkDrain; break;
	default: break;
	}
	Power = FMath::Clamp(Power - Rate * Dt, 0.0, MaxPower);
}

bool FSuit::TryPay(double Amount)
{
	if (bGodMode)
		return true;
	if (Power < Amount)
		return false;
	Power -= Amount;
	return true;
}

namespace
{
	/** Push the capsule out of terrain and kill velocity into the surface. */
	FCharacterState Resolve(FCharacterState State, FDensitySampler Density, FUpSampler SurfaceNormalAt, const FVector3d& Up)
	{
		using namespace CharacterMotion;
		// Two relaxation passes: push out along the local surface normal so a capsule wedged in a
		// dug wall slides free instead of being shoved toward the centre.
		for (int32 Pass = 0; Pass < 2; Pass++)
		{
			const double D = Density(State.Position);
			if (D >= Radius)
				break;
			const FVector3d N = SurfaceNormalAt(State.Position);
			State.Position += N * (Radius - D);
			const double Into = State.Velocity.Dot(N);
			if (Into < 0)
				State.Velocity -= N * Into;
		}
		// Grounded test: is there solid just below the feet, measured toward gravity?
		const double Below = Density(State.Position - Up * (Radius * 0.35));
		const FVector3d Floor = SurfaceNormalAt(State.Position);
		State.bGrounded = Below < Radius && Floor.Dot(Up) >= WalkableNormalDot && State.Velocity.Dot(Floor) <= 0.5;
		return State;
	}
}

FCharacterState CharacterMotion::Step(FCharacterState State, FDensitySampler Density, FUpSampler GravityUpAt, FUpSampler SurfaceNormalAt,
	const FVector3d& WishDir, bool bJump, bool bJetpack, bool bSprint, double Dt, double GravityFactor, const FVector3d* FlightWish)
{
	const bool bWasGrounded = State.bGrounded;
	State.bJustLanded = false;
	const FVector3d Up = GravityUpAt(State.Position);
	const double GravityScale = FMath::Clamp(GravityFactor, 0.0, 1.0);
	State.bWeightless = GravityScale <= 0.01;

	// --- desired horizontal motion in the tangent plane ---
	FVector3d TangentWish = WishDir - Up * WishDir.Dot(Up);
	if (TangentWish.SizeSquared() > 1e-6)
		TangentWish = TangentWish.GetSafeNormal() * (bSprint ? SprintSpeed : WalkSpeed);
	else
		TangentWish = FVector3d::ZeroVector;

	// Split velocity into along-up and tangential parts so one can be steered without
	// fighting the other.
	double Vertical = State.Velocity.Dot(Up);
	FVector3d Lateral = State.Velocity - Up * Vertical;

	const bool bFlying = FlightWish != nullptr && !State.bGrounded;
	if (bFlying)
	{
		// Powered six-axis flight: steer toward a view-relative velocity, brake on release, and
		// compensate gravity within the pack's acceleration limit.
		const FVector3d Target = FlightWish->SizeSquared() > 1e-6 ? FlightWish->GetSafeNormal() * (bSprint ? FastFlightSpeed : FlightSpeed)
			: FVector3d::ZeroVector;
		FVector3d Change = (Target - State.Velocity) * (1 - FMath::Exp(-AirControl * Dt)) + Up * (GravityAccel * GravityScale * Dt);
		const double Limit = (bSprint ? JetpackAccel * 3 : JetpackAccel) * Dt;
		if (Change.SizeSquared() > Limit * Limit)
			Change = Change.GetSafeNormal() * Limit;
		const FVector3d Velocity = State.Velocity + Change;
		Vertical = Velocity.Dot(Up);
		Lateral = Velocity - Up * Vertical;
	}
	else
	{
		const double Control = State.bGrounded ? GroundControl : AirControl;
		Lateral = Lerp(Lateral, TangentWish, 1 - FMath::Exp(-Control * Dt));
	}

	// --- vertical forces ---
	if (bJetpack && !bFlying)
	{
		Vertical += JetpackAccel * Dt;
	}
	else if (bJump && State.bGrounded)
	{
		Vertical = JumpSpeed;
		State.bGrounded = false;
	}

	// PINNED WHEN IDLE. An astronaut standing still is not integrated at all: falling a fraction
	// into the ground and being pushed back out along the surface normal leaves a net sideways
	// displacement every tick wherever gravity and the local normal disagree, which on bumpy
	// terrain is always. Scrubbing velocity afterwards cannot undo a move that already happened,
	// so the move must not happen.
	const bool bRestingStill = State.bGrounded && !bJump && !bJetpack && !State.bWeightless && TangentWish.SizeSquared() < 1e-12
		&& Lateral.Length() < StandingStopSpeed && Vertical <= 0;
	if (bRestingStill)
	{
		State.Velocity = FVector3d::ZeroVector;
		State = Resolve(State, Density, SurfaceNormalAt, Up); // still react to digs underfoot
		// The grounded probe reaches a little below the feet, so the first grounded tick of a
		// landing can be a few centimetres up: settle onto the ground rather than pin in the air
		// (and follow ground sculpted away underfoot the same way).
		double Gap = Density(State.Position) - Radius;
		for (int32 Pass = 0; Pass < 8 && Gap > 0.005 && Gap < GroundSnapDistance; Pass++)
		{
			State.Position -= Up * Gap;
			Gap = Density(State.Position) - Radius;
		}
		if (Gap < 0)
			State.Position += SurfaceNormalAt(State.Position) * -Gap;
		State.Up = GravityUpAt(State.Position);
		return State;
	}

	Vertical -= GravityAccel * GravityScale * Dt;
	Vertical = FMath::Clamp(Vertical, -MaxFallSpeed, MaxFallSpeed);

	// In true weightlessness let the jetpack push laterally too, and damp drift.
	if (State.bWeightless && !bFlying)
	{
		if (bJetpack && !TangentWish.IsZero())
			Lateral = Lerp(Lateral, TangentWish, 1 - FMath::Exp(-AirControl * Dt));
		Lateral *= FMath::Exp(-0.4 * Dt);
		Vertical *= FMath::Exp(-0.4 * Dt);
	}

	State.Velocity = Lateral + Up * Vertical;
	if (bWasGrounded && !bJump && !bJetpack && !State.bWeightless)
	{
		// Follow the walkable slope before integrating. Otherwise an uphill correction becomes
		// upward flight, and every downhill bump loses contact.
		const FVector3d Floor = SurfaceNormalAt(State.Position);
		if (Floor.Dot(Up) >= WalkableNormalDot)
			State.Velocity -= Floor * State.Velocity.Dot(Floor);
	}
	State.Position += State.Velocity * Dt;

	// --- resolve against the field (push out along the true surface normal, but test grounded
	//     toward gravity) ---
	State = Resolve(State, Density, SurfaceNormalAt, Up);

	// --- ground snap: keep a walking astronaut glued to the ground going downhill ---
	if (bWasGrounded && !bJump && !bJetpack && !State.bWeightless && SurfaceNormalAt(State.Position).Dot(Up) >= WalkableNormalDot)
	{
		double Gap = Density(State.Position) - Radius;
		if (Gap > 0.01 && Gap < GroundSnapDistance)
		{
			for (int32 Pass = 0; Pass < 8 && Gap > 0.01; Pass++)
			{
				State.Position -= Up * Gap;
				Gap = Density(State.Position) - Radius;
			}
		}
		// A changing slope can turn last tick's tangent velocity slightly outward. Continuous
		// nearby support is still walking, not a takeoff.
		if (Gap <= 0.02)
		{
			State.bGrounded = true;
			const FVector3d Floor = SurfaceNormalAt(State.Position);
			State.Velocity -= Floor * State.Velocity.Dot(Floor);
		}
	}

	if (!bWasGrounded && State.bGrounded)
	{
		State.bJustLanded = true;
		State.LandingSpeed = FMath::Max(0.0, -Vertical);
	}
	State.Up = GravityUpAt(State.Position);

	// STANDING STILL MUST ACTUALLY STAND STILL: with no input on the ground, scrub the
	// along-surface velocity - boots gripping rock.
	if (State.bGrounded && TangentWish.SizeSquared() < 1e-12)
	{
		const FVector3d Plane = SurfaceNormalAt(State.Position);
		const double AlongPlane = State.Velocity.Dot(Plane);
		FVector3d Slide = State.Velocity - Plane * AlongPlane;
		Slide *= FMath::Exp(-StandingFriction * Dt);
		if (Slide.Length() < StandingStopSpeed)
			Slide = FVector3d::ZeroVector;
		State.Velocity = Slide + Plane * AlongPlane;
	}
	return State;
}

ESuitActivity CharacterMotion::ActivityOf(const FCharacterState& State, bool bJetpack, const FVector3d& WishDir)
{
	if (State.bGrounded)
		return WishDir.SizeSquared() > 1e-6 ? ESuitActivity::Walking : ESuitActivity::Idle;
	if (bJetpack)
		return WishDir.SizeSquared() < 1e-6 && State.Velocity.SizeSquared() <= 0.0625 ? ESuitActivity::Hovering : ESuitActivity::Jetpack;
	return ESuitActivity::Idle;
}
