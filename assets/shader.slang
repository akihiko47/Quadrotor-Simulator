struct VSInput {
    float3 Pos;
    float3 Normal;
	float2 UV;
};

Sampler2D textures[];

struct PushConstants {
    ShaderData* shaderData;
    uint modelMatrixIndex;
    uint textureIndex;
    uint isLightBaked;
};

struct Light {
    float4 position;
    float4 direction;
    float4 color;
};

struct ShaderData {
    float4x4 projection;
    float4x4 view;
    float4x4 model[16];
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
    ShaderData *shaderData;
    float3 normal;
    float2 uv;
    float3 worldPos;
    uint32_t instanceIndex;
    uint32_t textureIndex;
    bool isLightBaked;
};

const static int groundInstanceIndex = 3;

float3 calculateDirectionalLight(Light light, float3 albedo, float3 normal, float3 viewDir) {
    float3 L = normalize(-light.direction.xyz);
    float diffuse = max(dot(normal, L), 0.0);
    float3 H = normalize(L + viewDir);
    //float specular = pow(max(dot(normal, H), 0.0), 32.0);
    float specular = 0.0;

    return diffuse * albedo * light.color.rgb + specular * light.color.rgb;
}

[shader("vertex")]
VSOutput main(VSInput input, uniform PushConstants pushConstants, uint instanceIndex: SV_VulkanInstanceID) {
    ShaderData shaderData = *pushConstants.shaderData;
    VSOutput output;
    output.shaderData = pushConstants.shaderData;

    if (instanceIndex == groundInstanceIndex) {
        output.normal = input.Normal;
        output.worldPos = input.Pos.xyz;
        output.pos = mul(shaderData.projection, mul(shaderData.view, float4(output.worldPos, 1.0)));
    } else {
        float4x4 modelMat = shaderData.model[pushConstants.modelMatrixIndex];
        output.normal = mul((float3x3)modelMat, input.Normal);
        output.worldPos = mul(modelMat, float4(input.Pos.xyz, 1.0)).xyz;
        output.pos = mul(shaderData.projection, mul(shaderData.view, float4(output.worldPos, 1.0)));
    }

    output.uv = input.UV;
    output.instanceIndex = instanceIndex;
    output.textureIndex = pushConstants.textureIndex;
    output.isLightBaked = pushConstants.isLightBaked;

    return output;
}

[shader("fragment")]
float4 main(VSOutput i) {
    ShaderData sd = *i.shaderData;

    // Phong lighting
    float3 N = normalize(i.normal);
    float3 L = normalize(-sd.lights[0].direction.xyz);
    float3 V = normalize(sd.camPos.xyz - i.worldPos);
    float3 R = reflect(-L, N);
    float3 diffuse = max(dot(N, L), 0.0025);
    float3 specular = pow(max(dot(R, V), 0.0), 16.0) * 0.75;

    // Sample from texture
    float3 albedo = float3(0);
    if (i.instanceIndex == groundInstanceIndex) {
        float cellSize = 1.0;
        float3 color1 = float3(0.4, 0.4, 0.4);
        float3 color2 = float3(0.75, 0.75, 0.75);

        float cellX = floor(i.worldPos.x / cellSize);
        float cellZ = floor(i.worldPos.z / cellSize);

        float parity = frac((cellX + cellZ) * 0.5) * 2.0;
        parity = floor(parity + 0.1);

        albedo += lerp(color1, color2, parity);
    } else {
        albedo += textures[NonUniformResourceIndex(i.textureIndex)].Sample(i.uv).rgb;
    }

    // Lights
    float3 color = float3(0);
    if (i.isLightBaked) {
        color += albedo;
    } else {
        color += calculateDirectionalLight(sd.lights[0], albedo, N, V);
        color += calculateDirectionalLight(sd.lights[1], albedo, N, V);
        color += calculateDirectionalLight(sd.lights[2], albedo, N, V);
    }

    // Ambient
    color += sd.ambientCol.rgb;

    // Fog
    float fogAmount = 1.0 - exp(-length(sd.camPos.xyz - i.worldPos) * sd.fog.w);
    float sunAmount = max(dot(-V, L), 0.0);
    float3 fogColor = lerp(sd.fog.rgb,  // blue
                           float3(1.0, 0.9, 0.7),  // yellow
                           pow(sunAmount, 8.0));
    color = lerp(color, fogColor, fogAmount);

    return float4(pow(color, 1.4), 1.0);
}