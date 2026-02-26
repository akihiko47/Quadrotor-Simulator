struct ShaderData {
    float4x4 projection;
    float4x4 view;
    float4x4 model[3];
    float4 lightPos;
    float4 lightCol;
    float4 fogCol;
    float4 ambientCol;
    float fogDensity;
    float time;
    uint32_t selected;
};

struct VSOutput {
    float4 Pos : SV_POSITION;
    float2 UV;
    float3 LightVec;
    float3 ViewVec;
    float3 fogCol;
    float3 ambientCol;
    float time;
};


[shader("vertex")]
VSOutput main(uint vertexId: SV_VertexID, uniform ShaderData *shaderData) {
    VSOutput output;

    output.UV = float2((vertexId << 1) & 2, vertexId & 2);
    output.Pos = float4(output.UV * 2.0 - 1.0, 1.0, 1.0);
    output.UV.y = 1.0 - output.UV.y;

    output.LightVec = shaderData.lightPos.xyz;
    output.ViewVec = float3(0,0,0);
    output.fogCol = shaderData.fogCol.rgb;
    output.ambientCol = shaderData.ambientCol.rgb;
    output.time = shaderData.time;

    return output;
}

[shader("fragment")]
float4 main(VSOutput input) : SV_TARGET {
    return float4(input.UV, 0.0, 1.0);
}