struct VSInput {
    float3 Pos;
    float3 Normal;
	float2 UV;
};

Sampler2D textures[];

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
    float3 normal;
    float2 uv;
    float3 factor;
    float3 lightDir;
    float3 lightCol;
    float3 worldPos;
    float3 camPos;
    uint32_t instanceIndex;
    float3 fogCol;
    float3 ambientCol;
};

const static int groundInstanceIndex = 3;

[shader("vertex")]
VSOutput main(VSInput input, uniform ShaderData *shaderData, uint instanceIndex : SV_VulkanInstanceID) {
    VSOutput output;
    if (instanceIndex == groundInstanceIndex) {
        output.normal = input.Normal;
        output.worldPos = input.Pos.xyz;
        output.pos = mul(shaderData->projection, mul(shaderData->view, float4(output.worldPos, 1.0)));
        output.factor = 1.0f;
        output.instanceIndex = instanceIndex;
    } else {
        float4x4 modelMat = shaderData->model[instanceIndex];
        output.normal = mul((float3x3)modelMat, input.Normal);
        output.worldPos = mul(modelMat, float4(input.Pos.xyz, 1.0)).xyz;
        output.pos = mul(shaderData->projection, mul(shaderData->view, float4(output.worldPos, 1.0)));
        output.factor = (shaderData->selected == instanceIndex ? 3.0f : 1.0f);
    }
    output.uv = input.UV;
    output.instanceIndex = instanceIndex;

    // Light
    output.lightDir = shaderData.lightDir.xyz;
    output.camPos = float3(shaderData.invV._m03, -shaderData.invV._m13, shaderData.invV._m23);
    output.lightCol = shaderData.lightCol.rgb;

    // Fog
    output.fogCol = shaderData.fogCol.rgb;

    // Ambient
    output.ambientCol = shaderData.ambientCol.rgb;

    return output;
}

[shader("fragment")]
float4 main(VSOutput i) {
    // Phong lighting
    float3 N = normalize(i.normal);
    float3 L = normalize(-i.lightDir);
    float3 V = normalize(i.camPos - i.worldPos);
    float3 R = reflect(-L, N);
    float3 diffuse = max(dot(N, L), 0.0025);
    float3 specular = pow(max(dot(R, V), 0.0), 16.0) * 0.75;

    // Sample from texture
    float3 color = float3(0);
    if (i.instanceIndex == groundInstanceIndex) {
        color += float3(0.8, 0.0, 0.0);
    } else {
        color += saturate(textures[NonUniformResourceIndex(i.instanceIndex)].Sample(i.uv).rgb * i.factor);
    }

    // Phong
    color = diffuse * color.rgb + specular * i.lightCol;

    // Fog

    // Ambient
    color += i.ambientCol;

    return float4(color, 1.0);
}