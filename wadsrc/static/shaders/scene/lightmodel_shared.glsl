#ifdef LIGHT_ATTENUATION_INVERSE_SQUARE

float distanceAttenuation(float dist, float radius)
{
	float strength = min(1500.0, (radius * radius) / 10);
	float a = dist / radius;
	float b = clamp(1.0 - a * a * a * a, 0.0, 1.0);
	return (b * b) / (dist * dist + 1.0) * strength;
}

#else //elif defined(LIGHT_ATTENUATION_LINEAR)

float distanceAttenuation(float dist, float radius)
{
	return clamp((radius - dist) / radius, 0.0, 1.0);
}

#endif
