"""Generates Stellar's few editor assets from code, so the repository needs no hand-made content.

Run from the repository root (adjust the engine path):
    "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "%CD%/Stellar.uproject" ^
        -run=pythonscript -script="%CD%/Scripts/build_content.py" -unattended -nosplash -nullrhi

Creates (overwriting):
    /Game/Materials/M_Terrain      vertex-coloured rock (colours come from the planet field and its geology),
                                   with detail noise at three scales read from the wrapped planet coordinates
                                   the mesher writes into UV0/UV1, and roughness from how bare the rock is
    /Game/Materials/M_Solid        Color / Roughness / Metallic / Emissive parameters for stand-ins and structures
    /Game/Materials/M_Ghost        translucent unlit Color: placement previews and selection
    /Game/Materials/M_Water        translucent water for the fluid spike
    /Game/Materials/M_BrushCursor  unlit, vertex-coloured: the sculpt brush's contour
    /Game/Maps/Planet              an empty level: AStellarWorld builds the planet and sky at runtime
"""

import unreal

MEL = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
assets = unreal.EditorAssetLibrary


def fresh(name, path, cls, factory):
    full = f"{path}/{name}"
    if assets.does_asset_exist(full):
        assets.delete_asset(full)
    return tools.create_asset(name, path, cls, factory)


def expression(material, cls, x, y):
    return MEL.create_material_expression(material, cls, x, y)


def constant(material, value, x, y):
    c = expression(material, unreal.MaterialExpressionConstant, x, y)
    c.set_editor_property("r", value)
    return c


def noise(material, position, scale, levels, lo, hi, x, y):
    n = expression(material, unreal.MaterialExpressionNoise, x, y)
    n.set_editor_property("noise_function", unreal.NoiseFunction.NOISEFUNCTION_GRADIENT_TEX)
    n.set_editor_property("scale", scale)
    n.set_editor_property("levels", levels)
    n.set_editor_property("output_min", lo)
    n.set_editor_property("output_max", hi)
    MEL.connect_material_expressions(position, "", n, "Position")
    return n


def multiply(material, a, b, x, y, a_out="", b_out=""):
    m = expression(material, unreal.MaterialExpressionMultiply, x, y)
    MEL.connect_material_expressions(a, a_out, m, "A")
    MEL.connect_material_expressions(b, b_out, m, "B")
    return m


def terrain_material():
    m = fresh("M_Terrain", "/Game/Materials", unreal.Material, unreal.MaterialFactoryNew())
    color = expression(m, unreal.MaterialExpressionVertexColor, -1200, 0)
    # The planet-frame position in metres, precise anywhere on a 1000 km planet: the component's
    # origin wrapped to 4096 m (custom primitive data 0-2, set per terrain node and rock tile)
    # plus the vertex's local position. (Not UVs: procedural mesh UVs are half floats, which
    # resolve only metres at these values.)
    origin = expression(m, unreal.MaterialExpressionVectorParameter, -1500, 300)
    origin.set_editor_property("parameter_name", "NodeOrigin")
    origin.set_editor_property("use_custom_primitive_data", True)
    origin.set_editor_property("primitive_data_index", 0)
    rgb = expression(m, unreal.MaterialExpressionComponentMask, -1300, 300)
    for channel in ("r", "g", "b"):
        rgb.set_editor_property(channel, True)
    MEL.connect_material_expressions(origin, "", rgb, "")
    local = expression(m, unreal.MaterialExpressionLocalPosition, -1500, 460)
    metres = expression(m, unreal.MaterialExpressionDivide, -1300, 460)
    metres.set_editor_property("const_b", 100.0)
    MEL.connect_material_expressions(local, "", metres, "A")
    position = expression(m, unreal.MaterialExpressionAdd, -1100, 360)
    MEL.connect_material_expressions(rgb, "", position, "A")
    MEL.connect_material_expressions(metres, "", position, "B")
    # Grain (half a metre), patches (a few metres) and broad variation (a hundred metres).
    fine = noise(m, position, 2.0, 3, 0.86, 1.10, -850, 260)
    mid = noise(m, position, 0.25, 4, 0.84, 1.12, -850, 420)
    macro = noise(m, position, 0.01, 3, 0.9, 1.1, -850, 580)
    tint = multiply(m, color, fine, -550, 0)
    tint = multiply(m, tint, mid, -400, 100)
    tint = multiply(m, tint, macro, -250, 200)
    MEL.connect_material_property(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # Vertex alpha: how bare the rock is (0 dust, 1 rock) - dust is matte, rock a little glossier.
    rough = expression(m, unreal.MaterialExpressionLinearInterpolate, -400, 400)
    rough.set_editor_property("const_a", 0.96)
    rough.set_editor_property("const_b", 0.74)
    MEL.connect_material_expressions(color, "A", rough, "Alpha")
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(constant(m, 0.32, -400, 560), "", unreal.MaterialProperty.MP_SPECULAR)

    # No normal perturbation: a screen-space-derivative bump from this noise rendered as pixel
    # blocks at every amplitude tried (2026-11-02). Relief comes from the 0.25 m terrain geometry
    # and the scattered rocks; a real detail normal needs an authored rock texture set.
    MEL.recompile_material(m)
    assets.save_asset(m.get_path_name())


def solid_material():
    m = fresh("M_Solid", "/Game/Materials", unreal.Material, unreal.MaterialFactoryNew())
    # Stand-ins are kitbashed into instanced components.
    m.set_editor_property("used_with_instanced_static_meshes", True)
    color = expression(m, unreal.MaterialExpressionVectorParameter, -600, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.5, 0.5, 0.5, 1))
    MEL.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = expression(m, unreal.MaterialExpressionScalarParameter, -600, 200)
    rough.set_editor_property("parameter_name", "Roughness")
    rough.set_editor_property("default_value", 0.7)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    metal = expression(m, unreal.MaterialExpressionScalarParameter, -600, 300)
    metal.set_editor_property("parameter_name", "Metallic")
    metal.set_editor_property("default_value", 0.0)
    MEL.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    glow = expression(m, unreal.MaterialExpressionScalarParameter, -600, 400)
    glow.set_editor_property("parameter_name", "Emissive")
    glow.set_editor_property("default_value", 0.0)
    MEL.connect_material_property(multiply(m, color, glow, -350, 400), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(m)
    assets.save_asset(m.get_path_name())


def ghost_material():
    m = fresh("M_Ghost", "/Game/Materials", unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("two_sided", True)
    color = expression(m, unreal.MaterialExpressionVectorParameter, -600, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.2, 0.9, 0.4, 1))
    MEL.connect_material_property(multiply(m, color, constant(m, 1.2, -600, 200), -350, 0), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(constant(m, 0.38, -350, 200), "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(m)
    assets.save_asset(m.get_path_name())


def water_material():
    m = fresh("M_Water", "/Game/Materials", unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    color = expression(m, unreal.MaterialExpressionConstant3Vector, -600, 0)
    color.set_editor_property("constant", unreal.LinearColor(0.06, 0.17, 0.2, 1))
    MEL.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(constant(m, 0.03, -600, 150), "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(constant(m, 1.0, -600, 250), "", unreal.MaterialProperty.MP_SPECULAR)
    MEL.connect_material_property(constant(m, 0.72, -600, 350), "", unreal.MaterialProperty.MP_OPACITY)
    # A faint scatter of skylight, so a pond in shadow still reads as water, not a hole.
    glow = expression(m, unreal.MaterialExpressionConstant3Vector, -600, 450)
    glow.set_editor_property("constant", unreal.LinearColor(0.01, 0.03, 0.035, 1))
    MEL.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(m)
    assets.save_asset(m.get_path_name())


def cursor_material():
    m = fresh("M_BrushCursor", "/Game/Materials", unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    color = expression(m, unreal.MaterialExpressionVertexColor, -600, 0)
    MEL.connect_material_property(multiply(m, color, constant(m, 1.5, -600, 200), -350, 0), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(m)
    assets.save_asset(m.get_path_name())


def planet_map():
    world = fresh("Planet", "/Game/Maps", unreal.World, unreal.WorldFactory())
    assets.save_asset(world.get_path_name())


terrain_material()
solid_material()
ghost_material()
water_material()
cursor_material()
planet_map()
unreal.log("Stellar content built")
