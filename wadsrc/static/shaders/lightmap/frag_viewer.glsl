
#include <shaders/lightmap/binding_viewer.glsl>
#include <shaders/lightmap/polyfill_rayquery.glsl>
#include <shaders/lightmap/trace_levelmesh.glsl>
#include <shaders/lightmap/trace_sunlight.glsl>
#include <shaders/lightmap/trace_light.glsl>
#include <shaders/lightmap/trace_ambient_occlusion.glsl>

layout(location = 0) out vec4 fragcolor;

vec3 skyColor(vec3 direction)
{
	if (direction.y > 0.0f)
		return mix(vec3(0.6, 0.25, 0.15), vec3(0.8, 0.5, 0.5), direction.y);
	else
		return vec3(0.03);
}

uint stepRNG(uint rngState)
{
	return rngState * 747796405 + 1;
}

float stepAndOutputRNGFloat(inout uint rngState)
{
	rngState = stepRNG(rngState);
	uint word = ((rngState >> ((rngState >> 28) + 4)) ^ rngState) * 277803737;
	word = (word >> 22) ^ word;
	return float(word) / 4294967295.0f;
}

void main()
{
	const int NUM_SAMPLES = 64;//64;
	const int MAX_BOUNCES = 8;//32;

	uint rngState = uint(gl_FragCoord.x) + 8720 * uint(gl_FragCoord.y);
	vec3 summedPixelColor = vec3(0.0);

	for(int sampleIdx = 0; sampleIdx < NUM_SAMPLES; sampleIdx++)
	{
		vec3 rayOrigin = CameraPos;

		const vec2 randomPixelCenter = gl_FragCoord.xy - 0.5 + vec2(stepAndOutputRNGFloat(rngState), stepAndOutputRNGFloat(rngState));
		vec4 viewpos = vec4(randomPixelCenter * vec2(ResolutionScaleX, ResolutionScaleY) - 1.0, -1.0, 1.0);
		viewpos.x /= ProjX;
		viewpos.y /= ProjY;
		vec3 rayDirection = normalize(ViewX * viewpos.x + ViewY * viewpos.y - ViewZ);

		vec3 accumulatedRayColor = vec3(1.0);
		for(int tracedSegments = 0; tracedSegments < MAX_BOUNCES; tracedSegments++)
		{
			TraceResult result = TraceFirstHit(rayOrigin, 0.0, rayDirection, 100000.0);
			if (result.primitiveIndex != -1)
			{
				SurfaceInfo surface = GetSurface(result.primitiveIndex);
				if (surface.Sky == 0.0)
				{
					vec4 color;
					if (surface.TextureIndex != 0)
					{
						vec2 uv = GetSurfaceUV(result.primitiveIndex, result.primitiveWeights);
						color = texture(textures[surface.TextureIndex], uv);
					}
					else
					{
						// Hit a surface without a texture
						color = vec4(0.0);
					}

					vec3 worldPos = GetSurfacePos(result.primitiveIndex, result.primitiveWeights);
					vec3 lightRayOrigin = worldPos + 0.001 * surface.Normal;

					if (color.a > 0.5)
					{
						accumulatedRayColor *= color.rgb;

						/*
						uint LightStart = surface.LightStart;
						uint LightEnd = surface.LightEnd;
						if (LightStart != LightEnd && stepAndOutputRNGFloat(rngState) < 0.5)
						{
							// This is total BS - pretend we hit the lights 50% of the time
							// We need some kind of physical representation for the lights

							vec3 incoming = vec3(0.0);
							//incoming += TraceSunLight(lightRayOrigin, surface.Normal);
							for (uint j = LightStart; j < LightEnd; j++)
								incoming += TraceLight(lightRayOrigin, surface.Normal, lights[lightIndexes[j]], 0.0, false);
							summedPixelColor += accumulatedRayColor * incoming;
							break;
						}
						*/

						rayOrigin = worldPos + 0.001 * surface.Normal;

						// Mirror reflect 10% of the time to create a glossy surface
						/*if (stepAndOutputRNGFloat(rngState) < 0.1)
						{
							// Mirror reflection:
							rayDirection = reflect(rayDirection, surface.Normal);
						}
						else*/
						{
							// Lambertian diffuse reflection:
							// Generate a random point on a sphere of radius 1 centered at the normal
							const float theta = 6.2831853 * stepAndOutputRNGFloat(rngState); // Random in [0, 2pi]
							const float u = 2.0 * stepAndOutputRNGFloat(rngState) - 1.0; // Random in [-1, 1]
							const float r = sqrt(1.0 - u * u);
							rayDirection = surface.Normal + vec3(r * cos(theta), r * sin(theta), u);
							rayDirection = normalize(rayDirection);
						}
					}
					else
					{
						// Transparent
						rayOrigin = worldPos - 0.001 * surface.Normal;
					}
				}
				else
				{
					// Hit the sky
					summedPixelColor += accumulatedRayColor * skyColor(rayDirection);
					break;
				}
			}
			else
			{
				// Ray hit nothing
				break;
			}
		}
	}

	fragcolor = vec4(summedPixelColor / float(NUM_SAMPLES), 1.0);
}
