
vec4 rayColor;

vec4 alphaBlend(vec4 a, vec4 b);
vec4 BeerLambertSimple(vec4 medium, vec4 ray_color);
vec4 blend(vec4 a, vec4 b);

int TraceFirstHitTriangleT(vec3 origin, float tmin, vec3 dir, float tmax, out float t)
{
	int primitiveID = -1;
	vec3 primitiveWeights;
	for (int i = 0; i < 4; i++)
	{
		primitiveID = TraceFirstHitTriangleNoPortal(origin, tmin, dir, tmax, t, primitiveWeights);

		if(primitiveID < 0)
		{
			break;
		}

		SurfaceInfo surface = surfaces[surfaceIndices[primitiveID]];

		if(surface.PortalIndex == 0)
		{
			int index = primitiveID * 3;
			vec2 uv = vertices[elements[index + 1]].uv * primitiveWeights.x + vertices[elements[index + 2]].uv * primitiveWeights.y + vertices[elements[index + 0]].uv * primitiveWeights.z;

			if (surface.TextureIndex == 0)
			{
				break;
			}

			vec4 color = texture(textures[surface.TextureIndex], uv);
			color.w *= surface.Alpha;

			if (color.w > 0.999 || all(lessThan(rayColor.rgb, vec3(0.001))))
			{
				break;
			}

			rayColor = blend(color, rayColor);
		}

		// Portal was hit: Apply transformation onto the ray
		mat4 transformationMatrix = portals[surface.PortalIndex].Transformation;

		origin = (transformationMatrix * vec4(origin + dir * t, 1.0)).xyz;
		dir = (transformationMatrix * vec4(dir, 0.0)).xyz;
		tmax -= t;
	}
	return primitiveID;
}

int TraceFirstHitTriangle(vec3 origin, float tmin, vec3 dir, float tmax)
{
	float t;
	return TraceFirstHitTriangleT(origin, tmin, dir, tmax, t);
}

bool TraceAnyHit(vec3 origin, float tmin, vec3 dir, float tmax)
{
	return TraceFirstHitTriangle(origin, tmin, dir, tmax) >= 0;
}

bool TracePoint(vec3 origin, vec3 target, float tmin, vec3 dir, float tmax)
{
	int primitiveID;
	float t;
	vec3 primitiveWeights;
	for (int i = 0; i < 4; i++)
	{
		t = tmax;
		primitiveID = TraceFirstHitTriangleNoPortal(origin, tmin, dir, tmax, t, primitiveWeights);

		origin += dir * t;
		tmax -= t;

		if(primitiveID < 0)
		{
			// We didn't hit anything
			break;
		}

		SurfaceInfo surface = surfaces[surfaceIndices[primitiveID]];

		if (surface.PortalIndex == 0)
		{
			int index = primitiveID * 3;
			vec2 uv = vertices[elements[index + 1]].uv * primitiveWeights.x + vertices[elements[index + 2]].uv * primitiveWeights.y + vertices[elements[index + 0]].uv * primitiveWeights.z;

			if (surface.TextureIndex == 0)
			{
				break;
			}

			vec4 color = texture(textures[surface.TextureIndex], uv);
			color.w *= surface.Alpha;

			if (color.w > 0.999 || all(lessThan(rayColor.rgb, vec3(0.001))))
			{
				break;
			}

			rayColor = blend(color, rayColor);
		}

		if(dot(surface.Normal, dir) >= 0.0)
		{
			continue;
		}

		mat4 transformationMatrix = portals[surface.PortalIndex].Transformation;
		origin = (transformationMatrix * vec4(origin, 1.0)).xyz;
		dir = (transformationMatrix * vec4(dir, 0.0)).xyz;
	}

	return distance(origin, target) <= 1.0;
}

vec4 alphaBlend(vec4 a, vec4 b)
{
	float na = a.w + b.w * (1.0 - a.w);
	return vec4((a.xyz * a.w + b.xyz * b.w * (1.0 - a.w)) / na, max(0.001, na));
}

vec4 BeerLambertSimple(vec4 medium, vec4 ray_color) // based on Beer-Lambert law
{
	float z = medium.w;
	ray_color.rgb *= exp(-medium.rgb * vec3(z));
	return ray_color;
}

vec4 blend(vec4 a, vec4 b)
{
	return BeerLambertSimple(vec4(1.0 - a.rgb, a.w), b);
}

