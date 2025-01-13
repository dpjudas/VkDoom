
void SetupMaterial(inout Material material)
{
	#ifdef SIMPLE3D
		material.Base = uObjectColor;
	#else
		material.Base = desaturate(uObjectColor);
	#endif
}
