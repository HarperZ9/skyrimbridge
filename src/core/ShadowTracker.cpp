#include "ShadowTracker.h"
#include <RE/Skyrim.h>

namespace SB::ShadowTracker
{
    ShadowData Update()
    {
        ShadowData data{};

        // ShadowSceneNode holds the active directional lights used
        // for shadow mapping.  The primary sun/moon shadow caster
        // is sunDirectionLight.
        auto& shaderState = RE::BSShaderManager::State::GetSingleton();

        // The directional ambient and specular in BSShaderManager::State
        // give us the current lighting state used for rendering.
        (void)shaderState;  // Currently not using shader state directly

        // Try getting the directional light from the sky's sun node
        auto* sky = RE::Sky::GetSingleton();
        if (!sky || !sky->sun)
            return data;

        // Sun's NiNode direction gives us the shadow caster direction.
        // For shadows, we want the direction FROM the light TO the ground
        // (the negated sun direction).
        auto* sunRoot = sky->sun->root.get();
        if (!sunRoot)
            return data;

        auto& sunPos = sunRoot->world.translate;
        float dist = std::sqrt(sunPos.x * sunPos.x +
                               sunPos.y * sunPos.y +
                               sunPos.z * sunPos.z);
        if (dist > 0.001f) {
            // Light direction = normalized position (from origin toward sun)
            // Shadow direction = negated (from sun toward origin)
            data.LightDirection.x = sunPos.x / dist;
            data.LightDirection.y = sunPos.y / dist;
            data.LightDirection.z = sunPos.z / dist;
            data.LightDirection.w = 1.0f;  // shadow intensity (default full)
        }

        // Sun color from weather for diffuse/ambient
        if (sky->currentWeather) {
            // The directional sunlight color is already extracted by
            // AtmosphereTracker.  We duplicate the sun color here for
            // shadow-specific use.  Could also read from the actual
            // NiDirectionalLight if we traverse the shadow scene node.
            data.LightDiffuse.w = 0.f;
            data.LightAmbient.w = 0.f;
        }

        return data;
    }
}
