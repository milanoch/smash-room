#include "panda/brdf.hlsli"
#include "panda/sun.hlsli"

// Preserve Object_Glass.mat's names, types and numeric-field order. In
// particular baseColorFactor remains a linear Vec4, not an @color field.
cbuffer MATERIAL_FIELDS {
    float4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    float4 emissiveFactor;
    uint alphaMode;
    float alphaCutoff;
    uint doubleSided;
    float glassEnvironmentIntensity;
};

Texture2D baseColorTexture;
SamplerState baseColorTextureSampler;
Texture2D metallicRoughnessTexture;
SamplerState metallicRoughnessTextureSampler;
Texture2D normalTexture;
SamplerState normalTextureSampler;
Texture2D occlusionTexture;
SamplerState occlusionTextureSampler;
Texture2D emissiveTexture;
SamplerState emissiveTextureSampler;
Texture2D environmentMap;
SamplerState environmentMapSampler;
// Explicit material binding for physical glass, shared by Edit and Play.
// VFX retain the engine's ordinary environmentMap path below.
Texture2D glassEnvironmentMap;
SamplerState glassEnvironmentMapSampler;

struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float2 fragTexCoord : TEXCOORD2;
    float4 fragColor : TEXCOORD3;
    float fragLight : TEXCOORD4;
    nointerpolation uint fragId : TEXCOORD5;
    bool isFrontFace : SV_IsFrontFace;
};

struct PSOutput {
    float4 color : SV_Target0;
    float4 emissive : SV_Target1;
};

float3 surfaceNormal(PSInput input) {
    float3 geometric = normalize(input.worldNormal);
    float3 sampled = normalTexture.Sample(normalTextureSampler, input.fragTexCoord).xyz * 2.0 - 1.0;
    float3 dpdx = ddx(input.worldPos);
    float3 dpdy = ddy(input.worldPos);
    float2 duvdx = ddx(input.fragTexCoord);
    float2 duvdy = ddy(input.fragTexCoord);
    float determinant = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
    float3 normal = geometric;
    if (abs(determinant) >= 0.00001) {
        float3 tangent = normalize((dpdx * duvdy.y - dpdy * duvdx.y) / determinant);
        float3 bitangent = normalize((dpdy * duvdx.x - dpdx * duvdy.x) / determinant);
        normal = normalize(tangent * sampled.x * normalScale + bitangent * sampled.y * normalScale +
                           geometric * sampled.z);
    }
    if (doubleSided != 0 && !input.isFrontFace) { normal = -normal; }
    return normal;
}

// Exactly the specular term of panda/brdf.hlsli::shadeLight. Track it separately
// from that helper's unchanged total lighting so VFX retain ordinary PBR.
float3 reflectedLight(float3 normal, float3 viewDirection, float3 lightDirection,
                      float3 radiance, float roughness, float3 f0) {
    float nDotL = saturate(dot(normal, lightDirection));
    if (nDotL <= 0.0) { return float3(0.0, 0.0, 0.0); }
    float3 halfVector = normalize(viewDirection + lightDirection);
    float nDotV = saturate(dot(normal, viewDirection));
    float ndf = distributionGGX(normal, halfVector, roughness);
    float geometry = geometrySmith(nDotV, nDotL, roughness);
    float3 fresnel = fresnelSchlick(saturate(dot(halfVector, viewDirection)), f0);
    return (ndf * geometry * fresnel) / max(4.0 * nDotV * nDotL, 0.0001) * radiance * nDotL;
}

float2 environmentBrdf(float roughness, float nDotV) {
    float4 r = roughness * float4(-1.0, -0.0275, -0.572, 0.022) +
                          float4(1.0, 0.0425, 1.04, -0.04);
    float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

bool isPhysicalGlass(float2 uv) {
    uint width, height;
    baseColorTexture.GetDimensions(width, height);
    // Existing GlassSparkleBurst atlas contract: physical glass uses column
    // 262.5; glints/flash use 0..255.5; chips/cracks use 272.5. A normal texture
    // (the editor's white texture) means the entire surface is glass.
    bool runtimeAtlas = width == 288 && height == 256;
    return !runtimeAtlas || abs(uv.x * 288.0 - 262.5) < 0.5;
}

PSOutput main(PSInput input) {
    bool physicalGlass = isPhysicalGlass(input.fragTexCoord);
    float4 baseColor = baseColorTexture.Sample(baseColorTextureSampler, input.fragTexCoord) *
                       baseColorFactor * float4(input.fragColor.rgb, 1.0);
    float alpha = baseColor.a;
    if (alphaMode == 1 && alpha < alphaCutoff) { discard; }
    float outAlpha = alphaMode == 2 ? alpha : 1.0;
    float3 albedo = baseColor.rgb;
    float4 metalRough = metallicRoughnessTexture.Sample(metallicRoughnessTextureSampler, input.fragTexCoord);
    float roughness = clamp(roughnessFactor * metalRough.g, 0.04, 1.0);
    float metallic = saturate(metallicFactor * metalRough.b);
    float ao = lerp(1.0, occlusionTexture.Sample(occlusionTextureSampler, input.fragTexCoord).r,
                    saturate(occlusionStrength));
    float3 emissive = emissiveTexture.Sample(emissiveTextureSampler, input.fragTexCoord).rgb * emissiveFactor.rgb;
    float3 normal = surfaceNormal(input);
    float3 viewDirection = normalize(cameraPosition.xyz - input.worldPos);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float2 volume = volumeOcclusion(input.worldPos + normal * (0.5 * occlusionTexel.w));
    float skyVisibility = lerp(1.0, volume.x, occlusionParams.x);
    float3 lighting = float3(0.0, 0.0, 0.0);
    float3 reflection = float3(0.0, 0.0, 0.0);

    if (directionalDirectionIntensity.w > 0.0) {
        float3 lightDirection = normalize(directionalDirectionIntensity.xyz);
        float visibility = sunVisibility(input.worldPos, input.worldNormal, volume.y);
        float3 radiance = directionalColor.rgb * directionalDirectionIntensity.w * visibility;
        lighting += shadeLight(normal, viewDirection, lightDirection, radiance,
                               albedo / PI, metallic, roughness, f0);
        reflection += reflectedLight(normal, viewDirection, lightDirection, radiance, roughness, f0);
    }
    for (uint i = 0; i < sceneLightCounts.x; ++i) {
        float3 toLight = pointLightPositionsRange[i].xyz - input.worldPos;
        float lightDistance = length(toLight);
        float range = pointLightPositionsRange[i].w;
        if (range <= 0.0 || lightDistance >= range) { continue; }
        float attenuation = 1.0 - saturate(lightDistance / range) * saturate(lightDistance / range);
        float3 lightDirection = normalize(toLight);
        float3 radiance = pointLightColorsIntensity[i].rgb * pointLightColorsIntensity[i].w *
                          attenuation * attenuation;
        lighting += shadeLight(normal, viewDirection, lightDirection, radiance,
                               albedo / PI, metallic, roughness, f0);
        reflection += reflectedLight(normal, viewDirection, lightDirection, radiance, roughness, f0);
    }

    float3 ambient = ambientColorIntensity.rgb * ambientColorIntensity.w * albedo * ao * skyVisibility;
    if ((physicalGlass && glassEnvironmentIntensity > 0.0) ||
        (!physicalGlass && environmentParams.z > 0.5)) {
        float3 direction = reflect(-viewDirection, normal);
        float2 uv = float2(atan2(direction.z, direction.x), asin(clamp(-direction.y, -1.0, 1.0))) *
                    float2(0.1591, 0.3183) + 0.5;
        float2 brdf = environmentBrdf(roughness, saturate(dot(normal, viewDirection)));
        float3 environmentRadiance;
        if (physicalGlass) {
            uint width, height, levels;
            glassEnvironmentMap.GetDimensions(0, width, height, levels);
            float highestMip = float(max(levels, 1u) - 1u);
            environmentRadiance = glassEnvironmentMap.SampleLevel(glassEnvironmentMapSampler, uv,
                                  roughness * highestMip).rgb * glassEnvironmentIntensity;
        } else {
            environmentRadiance = environmentMap.SampleLevel(environmentMapSampler, uv,
                                  roughness * environmentParams.y).rgb * environmentParams.x;
        }
        float3 reflectedEnvironment = environmentRadiance * (f0 * brdf.x + brdf.y) * ao * skyVisibility;
        ambient += reflectedEnvironment;
        reflection += reflectedEnvironment;
    }

    float3 color = (ambient + lighting) * input.fragLight + emissive;
    if (alphaMode == 2 && outAlpha > 0.0001 && physicalGlass) {
        // Fixed Panda blend is SrcAlpha / OneMinusSrcAlpha. Encoding R/a here
        // produces a*D + R + (1-a)*background; the final reflected radiance R
        // is the original GGX/IBL result, not an arbitrary brightness gain.
        // Keep alpha and background transmission EXACTLY as authored. This is
        // a thin-layer approximation, not full energy-conserving refraction.
        color += reflection * input.fragLight * (1.0 / outAlpha - 1.0);
    }
    PSOutput output;
    output.color = float4(color, outAlpha); // HDR: do not clamp before blending.
    output.emissive = float4(emissive, outAlpha); // Reflection must not enter bloom.
    return output;
}
