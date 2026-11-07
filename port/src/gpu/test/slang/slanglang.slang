// A shader in the Slang shading language (not RetroArch's GLSL .slang):
// slangtest checks that it is told apart and refused with a note.
struct VOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VOut vertexMain(float4 position : POSITION, float2 uv : TEXCOORD0) {
    VOut o;
    o.position = position;
    o.uv = uv;
    return o;
}

Sampler2D Source;

[shader("fragment")]
float4 fragmentMain(VOut i) : SV_Target {
    return Source.Sample(i.uv);
}
