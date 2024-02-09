
vec4 BeerLambertSimple(vec4 medium, vec4 ray_color);

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

vec4 BlendTexture(SurfaceInfo surface, vec2 uv, vec4 rayColor)
{
	if (surface.TextureIndex == 0)
	{
		return rayColor;
	}
	else
	{
		vec4 color = texture(textures[surface.TextureIndex], uv);
		return BeerLambertSimple(vec4(1.0 - color.rgb, color.a * surface.Alpha), rayColor);
	}
}

void TransformRay(uint portalIndex, inout vec3 origin, inout vec3 dir)
{
	mat4 transformationMatrix = portals[portalIndex].Transformation;
	origin = (transformationMatrix * vec4(origin, 1.0)).xyz;
	dir = (transformationMatrix * vec4(dir, 0.0)).xyz;
}

vec4 BeerLambertSimple(vec4 medium, vec4 ray_color) // based on Beer-Lambert law
{
	float z = medium.w;
	ray_color.rgb *= exp(-medium.rgb * vec3(z));
	return ray_color;
}
