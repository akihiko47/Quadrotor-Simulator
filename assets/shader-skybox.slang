struct ShaderData {
    float4x4 projection;
    float4x4 view;
    float4x4 model[3];
    float4x4 invP;
    float4x4 invV;
    float4 camPos;
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
    float3 lightCol;
    float3 fogCol;
    float3 ambientCol;
    float time;
};

float3 computeRayDir(float2 uv, float4x4 invPV) {
    float2 uvCenter = uv * 2.0 - 1.0;
    float4 rayDir = float4(uvCenter.x * sin(radians(70.0 / 2.0)), uvCenter.y * sin(radians(70.0 / 2.0)), 1, 1);
    rayDir = normalize(rayDir);
    rayDir = normalize(mul(invPV, rayDir));
    float3 rd = rayDir.xyz;
    return rd;
}

[shader("vertex")]
VSOutput main(uint vertexId: SV_VertexID, uniform ShaderData *shaderData) {
    VSOutput output;

    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.pos = float4(output.uv * 2.0 - 1.0, 1.0, 1.0);

    output.invPV = mul(shaderData.invV, shaderData.invP);
    output.lightDir = normalize(shaderData.lightDir.xyz);
    output.lightCol = shaderData.lightCol.rgb;
    output.fogCol = shaderData.fogCol.rgb;
    output.ambientCol = shaderData.ambientCol.rgb;
    output.time = shaderData.time;
    output.camPos = shaderData.camPos.xyz;

    return output;
}

[shader("fragment")]
float4 main(VSOutput i) : SV_TARGET {
    float3 L = normalize(-i.lightDir);
    float3 rd = computeRayDir(i.uv, i.invPV);

    float3 ro = i.camPos;

    // Fog
    float sunAmount = max(dot(rd, L), 0.0);
    float3 fogColor = lerp(i.fogCol,
                           float3(1.0, 0.9, 0.7), // yellow
                           pow(sunAmount, 8.0));

    // Skybox
    float skyMask = smoothstep(0.0, 0.5, rd.y);
    float groundMask = smoothstep(0.0, 0.3, -rd.y);
    float horizonMask = 1.0 - (skyMask + groundMask);
    float sunMask = smoothstep(0.9995, 0.9996, saturate(dot(rd, -i.lightDir)));

    float3 skyboxColor = skyMask * float3(0.53, 0.81, 0.92) + groundMask * float3(0.25, 0.28, 0.32) + horizonMask * fogColor;
    skyboxColor = skyboxColor * (1 - sunMask) + i.lightCol * sunMask;

    return float4(skyboxColor, 1.0);
}