/*
 Light probe actor.
*/

class LightProbe : Actor
{
	// note: thinker is set to STAT_INFO

	Default
	{
		+NOINTERACTION
		+NOBLOCKMAP
		+NOGRAVITY
		Height 2;
		Radius 1;
	}
}
