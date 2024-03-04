
vec3 BeerLambertSimple(vec3 medium, float depth, vec3 ray_color);

SurfaceInfo GetSurface(int primitiveIndex)
{
	return surfaces[surfaceIndices[primitiveIndex]];
}

vec2 GetSurfaceUV(int primitiveIndex, vec3 primitiveWeights)
{
	int index = primitiveIndex * 3;
	return
		vertices[elements[index + 1]].uv * primitiveWeights.x +
		vertices[elements[index + 2]].uv * primitiveWeights.y +
		vertices[elements[index + 0]].uv * primitiveWeights.z;
}

vec3 PassRayThroughSurface(SurfaceInfo surface, vec2 uv, vec3 rayColor)
{
	if (surface.TextureIndex == 0)
	{
		return rayColor;
	}
	else
	{
		vec4 color = texture(textures[surface.TextureIndex], uv);

		// To do: currently we do not know the material/renderstyle of the surface.
		//
		// This means we can't apply translucency and we can't do something like BeerLambertSimple.
		// In order to improve this SurfaceInfo needs additional info.
		//
		// return BeerLambertSimple(1.0 - color.rgb, color.a * surface.Alpha, rayColor);

		// Assume the renderstyle is basic alpha blend for now.
		return rayColor * (1.0 - color.a * surface.Alpha);
	}
}

void TransformRay(uint portalIndex, inout vec3 origin, inout vec3 dir)
{
	mat4 transformationMatrix = portals[portalIndex].Transformation;
	origin = (transformationMatrix * vec4(origin, 1.0)).xyz;
	dir = (transformationMatrix * vec4(dir, 0.0)).xyz;
}

vec3 BeerLambertSimple(vec3 medium, float depth, vec3 ray_color) // based on Beer-Lambert law
{
	return ray_color * exp(-medium * depth);
}
