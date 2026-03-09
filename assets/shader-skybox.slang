struct PushConstants {
    ShaderData* shaderData;
    uint modelMatrixIndex;
    uint textureIndex;
};

struct Light {
    float4 position;
    float4 direction;
    float4 color;
};

struct ShaderData {
    float4x4 projection;
    float4x4 view;
    float4x4 model[3];
    float4x4 invP;
    float4x4 invV;
    float4 camPos;
    float4 fog;
    float4 ambientCol;
    Light lights[3];
    float time;
};

struct VSOutput {
    float4 pos : SV_POSITION;
    ShaderData* shaderData;
    float4x4 invPV;
    float2 uv;
    float3 rayDir;
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
VSOutput main(uint vertexId: SV_VertexID, uniform PushConstants pushConstants) {
    ShaderData shaderData = *pushConstants.shaderData;
    VSOutput output;

    output.shaderData = pushConstants.shaderData;
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.pos = float4(output.uv * 2.0 - 1.0, 1.0, 1.0);
    output.invPV = mul(shaderData.invV, shaderData.invP);

    return output;
}

[shader("fragment")]
float4 main(VSOutput i) : SV_TARGET {
    ShaderData sd = *i.shaderData;

    float3 L = normalize(-sd.lights[0].direction.xyz);
    float3 rd = computeRayDir(i.uv, i.invPV);

    float3 ro = sd.camPos.xyz;

    // Fog
    float sunAmount = max(dot(rd, L), 0.0);
    float3 fogColor = lerp(sd.fog.rgb,
                           float3(1.0, 0.9, 0.7), // yellow
                           pow(sunAmount, 8.0));

    // Skybox
    float skyMask = smoothstep(0.0, 0.5, rd.y);
    float groundMask = smoothstep(0.0, 0.3, -rd.y);
    float horizonMask = 1.0 - (skyMask + groundMask);
    float sunMask = smoothstep(0.9995, 0.9996, saturate(dot(rd, L)));

    float3 skyboxColor = skyMask * float3(0.53, 0.81, 0.92) + groundMask * float3(0.25, 0.28, 0.32) + horizonMask * fogColor;
    skyboxColor = skyboxColor * (1 - sunMask) + sunMask;

    return float4(skyboxColor, 1.0);
}