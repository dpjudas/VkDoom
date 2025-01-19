
#ifdef LIGHT_ATTENUATION_INVERSE_SQUARE

float distanceAttenuation(float dist, float radius, float strength, float linearity)
{
	// light.radius >= 1000000.0 is sunlight, skip attenuation
	if(light.radius >= 1000000.0) return 1.0;
	float a = dist / radius;
	float b = clamp(1.0 - a * a * a * a, 0.0, 1.0);
	return mix((b * b) / (dist * dist + 1.0) * strength, clamp((radius - dist) / radius, 0.0, 1.0), linearity);
}

#else //elif defined(LIGHT_ATTENUATION_LINEAR)

#define distanceAttenuation(dist, radius, strength, linearity) clamp((radius - dist) / radius, 0.0, 1.0)

#endif
