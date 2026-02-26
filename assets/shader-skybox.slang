struct ShaderData {
    float4x4 projection;
    float4x4 view;
    float4x4 model[3];
    float4x4 invP;
    float4x4 invV;
    float4 lightDir;
    float4 lightCol;
    float4 fogCol;
    float4 ambientCol;
    float fogDensity;
    float time;
    uint32_t selected;
};

struct VSOutput {
    float4 pos : SV_POSITION;
    float4x4 invPV;
    float2 uv;
    float3 rayDir;
    float3 camPos;
    float3 lightDir;
    float3 fogCol;
    float3 ambientCol;
    float time;
};

[shader("vertex")]
VSOutput main(uint vertexId: SV_VertexID, uniform ShaderData *shaderData) {
    VSOutput output;

    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.pos = float4(output.uv * 2.0 - 1.0, 1.0, 1.0);
    output.uv.y = 1.0 - output.uv.y;

    output.invPV = mul(shaderData.invV, shaderData.invP);
    output.lightDir = shaderData.lightDir.xyz;
    output.fogCol = shaderData.fogCol.rgb;
    output.ambientCol = shaderData.ambientCol.rgb;
    output.time = shaderData.time;
    output.camPos = float3(shaderData.invV._m03, -shaderData.invV._m13, shaderData.invV._m23);

    return output;
}

[shader("fragment")]
float4 main(VSOutput input) : SV_TARGET {

    float2 uvCenter = input.uv * 2.0 - 1.0;
    float4 rayDir = float4(uvCenter.x * sin(radians(70.0 / 2.0)), uvCenter.y * sin(radians(70.0 / 2.0)), 1, 1);
    rayDir = normalize(rayDir);
    rayDir = normalize(mul(input.invPV, rayDir));
    float3 rd = rayDir.xyz;
    float3 ro = input.camPos;

    // Skybox
    float skyMask = smoothstep(0.0, 0.3, rd.y);
    float groundMask = smoothstep(0.0, 0.1, -rd.y);
    float horizonMask = 1.0 - (skyMask + groundMask);

    float3 skyboxColor = skyMask * float3(0.3, 0.5, 1.0) + groundMask * float3(0.25, 0.28, 0.32) + horizonMask * float3(0.7, 0.85, 1.0);

    return float4(skyboxColor, 1.0);
}