#pragma once

struct FFlatVertex // Note: this must always match the SurfaceVertex struct in shaders (std430 layout rules apply)
{
	float x, z, y;	// world position
	float lindex;	// lightmap texture index
	float u, v;		// texture coordinates
	float lu, lv;	// lightmap texture coordinates

	void Set(float xx, float zz, float yy, float uu, float vv)
	{
		x = xx;
		z = zz;
		y = yy;
		u = uu;
		v = vv;
		lindex = -1.0f;
	}

	void Set(float xx, float zz, float yy, float uu, float vv, float llu, float llv, float llindex)
	{
		x = xx;
		z = zz;
		y = yy;
		lindex = llindex;
		u = uu;
		v = vv;
		lu = llu;
		lv = llv;
	}

	void SetVertex(float _x, float _y, float _z = 0)
	{
		x = _x;
		z = _y;
		y = _z;
	}

	void SetTexCoord(float _u = 0, float _v = 0)
	{
		u = _u;
		v = _v;
	}

	FVector3 fPos() const { return FVector3(x, y, z); }
};
