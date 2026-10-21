import unreal
for name in ['T_FireBall_Fluid_07_E','T_FireBall_Fluid_07_MeshedDepth_8X8_PreMult','T_FireBall_07C_ResdiualSmokey']:
    tex=unreal.load_asset('/Game/NPC/ParagonGRIMexe/FX/Textures/Explosions/'+name)
    unreal.log('BOMB_TEXTURE '+name+' '+str({key:str(tex.get_editor_property(key)) for key in
        ['lod_group','lod_bias','max_texture_size','never_stream','filter','mip_gen_settings','srgb']}))
    unreal.log('BOMB_TEXTURE_SIZE '+str(tex.blueprint_get_size_x())+' '+str(tex.blueprint_get_size_y()))
