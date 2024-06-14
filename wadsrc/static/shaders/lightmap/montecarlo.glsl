
float RadicalInverse_VdC(uint bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10f; // / 0x100000000
}

vec2 Hammersley(uint i, uint N)
{
	return vec2(float(i) / float(N), RadicalInverse_VdC(i));
}

vec2 getVogelDiskSample(int sampleIndex, int sampleCount, float phi) 
{
    const float goldenAngle = radians(180.0) * (3.0 - sqrt(5.0));
    float sampleIndexF = float(sampleIndex);
    float sampleCountF = float(sampleCount);
    
    float r = sqrt((sampleIndexF + 0.5) / sampleCountF);  // Assuming index and count are positive
    float theta = sampleIndexF * goldenAngle + phi;
    
    float sine = sin(theta);
    float cosine = cos(theta);
    
    return vec2(cosine, sine) * r;
}
