
layout(set = 0, binding = 0) uniform sampler2DMS tex;

layout(location = 0) in vec2 TexCoord;
layout(location = 0) out vec4 fragcolor;

vec4 samplePixel(ivec2 pos, int samplecount)
{
	vec4 c = vec4(0.0);
	float count = 0.0;
	for (int i = 0; i < samplecount; i++)
	{
		vec4 p = texelFetch(tex, pos, i);
		if (p.a != -1.0)
		{
			c += p;
			count++;
		}
	}
	if (count == 0.0)
		return vec4(0.0, 0.0, 0.0, -1.0);
	else
		return c / count;
}

void main()
{
	int samplecount = textureSamples(tex);
	ivec2 size = textureSize(tex);
	ivec2 pos = ivec2(gl_FragCoord.xy);

	vec4 c = samplePixel(pos, samplecount);
	if (c.a == -1.0)
	{
		float count = 0.0;
		c = vec4(0.0, 0.0, 0.0, -1.0);
		for (int y = -1; y <= 1; y++)
		{
			for (int x = -1; x <= 1; x++)
			{
				if (x != 0 || y != 0)
				{
					ivec2 pos2;
					pos2.x = clamp(pos.x + x, 0, size.x - 1);
					pos2.y = clamp(pos.y + y, 0, size.y - 1);
					vec4 p = samplePixel(pos2, samplecount);
					if (p.a != -1.0)
					{
						c += p;
						count++;
					}
				}
			}
		}
		if (count != 0.0)
			c /= count;
	}

	fragcolor = c;
}
