using REAssetExplorer.Core.Render;

namespace REAssetExplorer.Games.RE7;

public class RE7ShaderSystemDeps : IShaderSystemDeps
{
    public IEnumerable<string> GetShaderSystemDeps()
    {
        return new[]
        {
            // rendering
            "systems/rendering/ambientbrdf.tex",
            "systems/rendering/areatex.tex",
            "systems/rendering/beach_cross_cm.tex",
            "systems/rendering/bluenoise.tex",
            "systems/rendering/bluenoise16x16.tex",
            "systems/rendering/colorcubelinear.tex",
            "systems/rendering/groundsmoothnoise.tex",
            "systems/rendering/inscatter.tex",
            "systems/rendering/irradiance.tex",
            "systems/rendering/lens_mask.tex",
            "systems/rendering/ltc1.tex",
            "systems/rendering/ltc2.tex",
            "systems/rendering/newambientbrdf.tex",
            "systems/rendering/nullatos.tex",
            "systems/rendering/nullblack.tex",
            "systems/rendering/nullblack3d.tex",
            "systems/rendering/nullblackcubemap.tex",
            "systems/rendering/nullgray.tex",
            "systems/rendering/nulllightmap.tex",
            "systems/rendering/nullnormal.tex",
            "systems/rendering/nullnormalroughness.tex",
            "systems/rendering/nullnormalroughnessocclusion.tex",
            "systems/rendering/nullwhite.tex",
            "systems/rendering/nullwhitecubemap.tex",
            "systems/rendering/searchtex.tex",
            "systems/rendering/speedtreegamewindnoise.tex",
            "systems/rendering/speedtreeperlinnoise.tex",
            "systems/rendering/transmittance.tex",
            "systems/rendering/volumenoise_lin.tex",
            "systems/rendering/weather.tex",
            
            // eyeball
            "systems/rendering/eyeball/eye_alb.tex",
            "systems/rendering/eyeball/eye_nmr.tex",
            "systems/rendering/eyeball/eye_occ.tex",
            "systems/rendering/eyeball/eye_vns.tex",
            "systems/rendering/eyeball/eyecaustics.tex",
        };
    }

    public string GameName  => "RE7";
}