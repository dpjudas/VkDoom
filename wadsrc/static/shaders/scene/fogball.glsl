
// The MIT License
// https://www.youtube.com/c/InigoQuilez
// https://iquilezles.org/
// Copyright (c) 2015 Inigo Quilez
//
// Analytically integrating quadratically decaying participating media within a sphere.
// Related info: https://iquilezles.org/articles/spherefunctions
float FogSphereDensity(vec3 rayOrigin, vec3 rayDirection, vec3 sphereCenter, float sphereRadius, float dbuffer)
{
	// normalize the problem to the canonical sphere
	float ndbuffer = dbuffer / sphereRadius;
	vec3 rc = (rayOrigin - sphereCenter) / sphereRadius;

	// find intersection with sphere
	float b = dot(rayDirection, rc);
	float c = dot(rc, rc) - 1.0f;
	float h = b * b - c;

	// not intersecting
	if (h < 0.0f) return 0.0f;

	h = sqrt(h);

	//return h*h*h;

	float t1 = -b - h;
	float t2 = -b + h;

	// not visible (behind camera or behind ndbuffer)
	if (t2 < 0.0f || t1 > ndbuffer) return 0.0f;

	// clip integration segment from camera to ndbuffer
	t1 = max(t1, 0.0f);
	t2 = min(t2, ndbuffer);

	// analytical integration of an inverse squared density
	float i1 = -(c * t1 + b * t1 * t1 + t1 * t1 * t1 * (1.0f / 3.0f));
	float i2 = -(c * t2 + b * t2 * t2 + t2 * t2 * t2 * (1.0f / 3.0f));
	return (i2 - i1) * (3.0f / 4.0f);
}

vec4 ProcessFogBalls(vec4 light)
{
	vec3 rayOrigin = uCameraPos.xyz;
	float dbuffer = distance(pixelpos.xyz, uCameraPos.xyz);
	vec3 rayDirection = normalize(pixelpos.xyz - uCameraPos.xyz);

	int first = uFogballIndex + 1;
	int last = uFogballIndex + int(fogballs[uFogballIndex].position.x);
	for (int i = first; i <= last; i++)
	{
		vec3 sphereCenter = fogballs[i].position.xzy;
		float sphereRadius = fogballs[i].radius;

		vec3 fogcolor = fogballs[i].color;
		float density = FogSphereDensity(rayOrigin, rayDirection, sphereCenter, sphereRadius, dbuffer) * fogballs[i].fog;
		float alpha = clamp(density, 0.0, 1.0);
		light.rgb = mix(light.rgb, fogcolor * density, alpha);
	}

	return light;
}
