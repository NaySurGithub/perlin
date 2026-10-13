// Lit vertex colour; with fancy dynamic lights the lightmap is sampled per pixel with the extra light added.
vec4 litColor(vec4 vertexColor, vec3 rel, vec2 lightBytes, vec3 tint)
{
    if (u_dynParams.x < 1.5)
        return vertexColor;
    float level = dynamicLevel(rel);
    vec2 light = vec2(max(lightBytes.x, level * 16.0), lightBytes.y);
    return vec4(tint * sampleLightmap(light), vertexColor.a);
}
