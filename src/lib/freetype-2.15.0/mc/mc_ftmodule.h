/* The game's FreeType modules (README-MC.md): TrueType faces (the menus' face, Saira Semi Condensed), drawn smooth.
   Read through FT_CONFIG_MODULES_H in place of include/freetype/config/ftmodule.h, which stays as released. */
FT_USE_MODULE( FT_Module_Class, autofit_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
