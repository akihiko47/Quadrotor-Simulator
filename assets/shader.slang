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
    output.camPos = shaderData.camPos.xyz;
    output.lightDir = normalize(shaderData.lightDir.xyz);
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
        float cellSize = 1.0;
        float3 color1 = float3(0.4, 0.4, 0.4);
        float3 color2 = float3(0.75, 0.75, 0.75);

        float cellX = floor(i.worldPos.x / cellSize);
        float cellZ = floor(i.worldPos.z / cellSize);

        float parity = frac((cellX + cellZ) * 0.5) * 2.0;
        parity = floor(parity + 0.1);

        color += lerp(color1, color2, parity);
    } else {
        color += saturate(textures[NonUniformResourceIndex(i.instanceIndex)].Sample(i.uv).rgb * i.factor);
    }

    // Phong
    color = diffuse * color.rgb + specular * i.lightCol;

    // Ambient
    color += i.ambientCol;

    // Fog
    float fogAmount = 1.0 - exp(-length(i.camPos - i.worldPos) * 0.04);
    float sunAmount = max(dot(-V, L), 0.0);
    float3 fogColor = lerp(i.fogCol,  // blue
                           float3(1.0, 0.9, 0.7),  // yellow
                           pow(sunAmount, 8.0));
    color = lerp(color, fogColor, fogAmount);


    return float4(color, 1.0);
}