#include "panda/vertex_ubo.hlsli"

// Panda's ordinary mesh layout, identical to the default lit/PBR vertex stage.
struct VSInput {
    float3 position : POSITION;
    float2 texCoord : TEXCOORD0;
    float3 normal : NORMAL;
    float4 color : COLOR0;
    float light : TEXCOORD1;
    nointerpolation uint id : TEXCOORD2;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float2 fragTexCoord : TEXCOORD2;
    float4 fragColor : TEXCOORD3;
    float fragLight : TEXCOORD4;
    nointerpolation uint fragId : TEXCOORD5;
};

VSOutput main(VSInput input) {
    VSOutput output;
    float4 worldPos = mul(modelMtx, float4(input.position, 1.0));
    output.position = mul(projViewMtx, worldPos);
    output.worldPos = worldPos.xyz;
    output.worldNormal = normalize(mul((float3x3)modelMtx, input.normal));
    output.fragTexCoord = input.texCoord;
    output.fragColor = input.color;
    output.fragLight = input.light;
    output.fragId = input.id;
    return output;
}
