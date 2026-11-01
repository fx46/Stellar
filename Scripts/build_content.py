"""Generates Stellar's few editor assets from code, so the repository needs no hand-made content.

Run from the repository root (adjust the engine path):
    "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "%CD%/Stellar.uproject" ^
        -run=pythonscript -script="%CD%/Scripts/build_content.py" -unattended -nosplash

Creates (overwriting):
    /Game/Materials/M_Terrain      vertex-coloured rock (colours come from the planet field and its geology)
    /Game/Materials/M_Solid        Color / Roughness / Metallic / Emissive parameters for stand-ins
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


def terrain_material():
    m = fresh("M_Terrain", "/Game/Materials", unreal.Material, unreal.MaterialFactoryNew())
    color = expression(m, unreal.MaterialExpressionVertexColor, -600, 0)
    # Fine grain from world position so close ground is not flat colour: two octaves of
    # noise scaled into a +-12% brightness variation.
    world = expression(m, unreal.MaterialExpressionWorldPosition, -900, 200)
    noise = expression(m, unreal.MaterialExpressionNoise, -700, 200)
    noise.set_editor_property("scale", 0.02)
    noise.set_editor_property("levels", 3)
    noise.set_editor_property("output_min", 0.88)
    noise.set_editor_property("output_max", 1.12)
    MEL.connect_material_expressions(world, "", noise, "Position")
    tint = expression(m, unreal.MaterialExpressionMultiply, -350, 0)
    MEL.connect_material_expressions(color, "", tint, "A")
    MEL.connect_material_expressions(noise, "", tint, "B")
    MEL.connect_material_property(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = expression(m, unreal.MaterialExpressionConstant, -350, 200)
    rough.set_editor_property("r", 0.93)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    spec = expression(m, unreal.MaterialExpressionConstant, -350, 300)
    spec.set_editor_property("r", 0.3)
    MEL.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    MEL.recompile_material(m)
    assets.save_asset(m.get_path_name())


def solid_material():
    m = fresh("M_Solid", "/Game/Materials", unreal.Material, unreal.MaterialFactoryNew())
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
    emissive = expression(m, unreal.MaterialExpressionMultiply, -350, 400)
    MEL.connect_material_expressions(color, "", emissive, "A")
    MEL.connect_material_expressions(glow, "", emissive, "B")
    MEL.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(m)
    assets.save_asset(m.get_path_name())


def cursor_material():
    m = fresh("M_BrushCursor", "/Game/Materials", unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    color = expression(m, unreal.MaterialExpressionVertexColor, -600, 0)
    boost = expression(m, unreal.MaterialExpressionMultiply, -350, 0)
    MEL.connect_material_expressions(color, "", boost, "A")
    gain = expression(m, unreal.MaterialExpressionConstant, -600, 200)
    gain.set_editor_property("r", 1.5)
    MEL.connect_material_expressions(gain, "", boost, "B")
    MEL.connect_material_property(boost, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(m)
    assets.save_asset(m.get_path_name())


def planet_map():
    world = fresh("Planet", "/Game/Maps", unreal.World, unreal.WorldFactory())
    assets.save_asset(world.get_path_name())


terrain_material()
solid_material()
cursor_material()
planet_map()
unreal.log("Stellar content built")
