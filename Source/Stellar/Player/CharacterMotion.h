#pragma once

#include "CoreMinimal.h"

/** Density of the world at a planet-frame point (negative inside solid). */
using FDensitySampler = TFunctionRef<double(const FVector3d& P)>;
/** A direction sampler: gravity up, or the local surface normal, depending on which is passed. */
using FUpSampler = TFunctionRef<FVector3d(const FVector3d& P)>;

/** Reference-frame rules for the astronaut's view (port of the Godot PlayerOrientation). */
namespace PlayerOrientation
{
	constexpr double MaxUpRadiansPerSecond = UE_DOUBLE_PI * 0.5;

	/** Turn Current toward Target at Rate (per second, exponential), capped in angular speed. */
	STELLAR_API FVector3d SmoothUp(const FVector3d& Current, const FVector3d& Target, double Rate, double Dt);

	/** Parallel transport of a heading from OldUp's tangent plane to NewUp's: avoids the yaw and
	 * roll that repeatedly projecting a heading onto a changing tangent plane introduces. */
	STELLAR_API FVector3d Transport(const FVector3d& Heading, const FVector3d& OldUp, const FVector3d& NewUp);
}

/** The astronaut's physical state between ticks (planet frame, metres). */
struct FCharacterState
{
	FVector3d Position = FVector3d::ZeroVector;
	FVector3d Velocity = FVector3d::ZeroVector;
	bool bGrounded = false;
	/** True when far enough from the planet that gravity has faded out. */
	bool bWeightless = false;
	/** The up the solver last stood the body along: gravity up. */
	FVector3d Up = FVector3d(0, 0, 1);
	/** Downward speed (along up) at the moment of the last landing, and true for exactly the
	 * tick it happened. */
	double LandingSpeed = 0;
	bool bJustLanded = false;
};

/** What the suit is doing this tick, for its power draw. */
enum class ESuitActivity : uint8 { Idle, Walking, Jetpack, Hovering };

/**
 * Space-bar arbitration between jumping and the jetpack: tap to jump (with coyote time and a
 * jump buffer so a press is never eaten); hold after a jump, or press again in the air, for the
 * jetpack - so tapping to jump never burns jetpack power.
 */
class STELLAR_API FJumpJetInput
{
public:
	static constexpr double CoyoteTime = 0.12;
	static constexpr double JumpBufferTime = 0.15;
	static constexpr double JetHoldDelay = 0.22;

	void Reset();

	/** This tick's intent: jump and/or fire the jetpack. */
	void Update(bool bSpace, bool bGrounded, bool bCanThrust, double Dt, bool& bOutJump, bool& bOutJet);

	/** After the motion step: refresh coyote time from the new grounded state. */
	void AfterStep(bool bGrounded, double Dt);

private:
	bool bSpaceWasHeld = false;
	double SpaceHeldTime = 0;
	double JumpBuffer = 0;
	double Coyote = 0;
	bool bJetArmed = false;
};

/** Suit battery (port of the Godot Suit): the jetpack and walking draw it, standing still
 * slowly tops it up, god mode keeps it full. Percent, 0-100. */
struct STELLAR_API FSuit
{
	static constexpr double MaxPower = 100;
	static constexpr double JetpackDrain = 7.5;
	static constexpr double HoverDrain = 0.05;
	static constexpr double WalkDrain = 0.55;
	static constexpr double PowerPerBite = 3.0;
	static constexpr double TrickleRate = 0.4;

	double Power = MaxPower;
	bool bGodMode = false;

	void Tick(double Dt, ESuitActivity Activity);
	bool CanThrust() const { return bGodMode || Power > 0.5; }
	bool TryPay(double Amount);
};

/**
 * Character physics against the SIGNED DISTANCE FIELD rather than collision meshes (port of the
 * Godot CharacterMotion). Each tick: sample density under the capsule, push out along the
 * gradient if penetrating, cancel velocity into the surface. Works at any LOD, cannot fall
 * through terrain that is still streaming in, and needs no collision cooking for the planet's
 * thousand terrain meshes. All in doubles: on a 1000 km planet a float resolves only 6 cm.
 *
 * Gravity points at the planet's centre (not the local density gradient, which wobbles near dug
 * lips). Walking is resolved in the tangent plane, so the planet can be circumnavigated.
 */
namespace CharacterMotion
{
	/** Capsule radius used for terrain resolution (also the eye offset). */
	constexpr double Radius = 0.9;
	/** Surface gravity (realistic register: about Earth's). */
	constexpr double GravityAccel = 9.81;
	constexpr double WalkSpeed = 6.0;
	constexpr double SprintSpeed = 11.0;
	constexpr double JumpSpeed = 5.2;
	constexpr double JetpackAccel = 18.0;
	constexpr double FlightSpeed = 12.0;
	constexpr double FastFlightSpeed = 40.0;
	/** Terminal-ish clamp so long falls stay controllable. */
	constexpr double MaxFallSpeed = 55.0;
	constexpr double GroundControl = 14.0;
	constexpr double AirControl = 3.5;
	/** Extra grip when standing with no input, so the astronaut does not creep downhill. */
	constexpr double StandingFriction = 24.0;
	/** Below this along-surface speed, an idle grounded astronaut is pinned. */
	constexpr double StandingStopSpeed = 0.75;
	/** How far below the feet the ground is still under a walking astronaut. */
	constexpr double GroundSnapDistance = 0.6;
	/** Steeper faces are walls, not floor contacts. */
	constexpr double WalkableNormalDot = 0.5;

	/**
	 * Advance the character one tick. WishDir is the desired horizontal direction (planet frame,
	 * from the camera heading). A non-null FlightWish enables powered view-relative flight: a
	 * direction, or zero for braking/hovering. GravityFactor scales gravity (falls off with
	 * distance from the planet).
	 */
	STELLAR_API FCharacterState Step(FCharacterState State, FDensitySampler Density, FUpSampler GravityUpAt, FUpSampler SurfaceNormalAt,
		const FVector3d& WishDir, bool bJump, bool bJetpack, bool bSprint, double Dt, double GravityFactor, const FVector3d* FlightWish);

	STELLAR_API ESuitActivity ActivityOf(const FCharacterState& State, bool bJetpack, const FVector3d& WishDir);
}
