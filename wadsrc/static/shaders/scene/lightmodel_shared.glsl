
#ifdef LIGHT_ATTENUATION_INVERSE_SQUARE

float distanceAttenuation(float dist, float radius, float strength)
{
	float a = dist / radius;
	float b = clamp(1.0 - a * a * a * a, 0.0, 1.0);
	return (b * b) / (dist * dist + 1.0) * strength;
}

#else //elif defined(LIGHT_ATTENUATION_LINEAR)

#define distanceAttenuation(dist, radius, strength) clamp((radius - dist) / radius, 0.0, 1.0)

#endif
