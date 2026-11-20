// FMA Mobile main-pass port. Textures are raw Gamma values, as in the source Unity Gamma player.
// Descriptor aliases exist only for the exclusive FMA modes: mask=source mask, tint=baked source COLOR0 data,
// detailMap=shadow, detailNormal=UV1 decal baked to UV0, emissive=SDF (R original, G mirrored),
// layerMask=GM_RampTex, normal=GM_RimRampTex, physical=GM_SSSRampTex. No PBR map interpretation applies.
// The GLM has no source TANGENT0 (a smoothed normal, NOT a UV tangent): Ng substitutes for it explicitly.
// Full SH / point lights, reflected-light TEXCOORD7, shadowEdge and adjustment-volume globals are unavailable.

float FmaRimCurve( float x )
{
	return 1.0 - x * ( x * ( 4.0 * x - 6.0 ) + 3.0 );
}

vec3 FmaColorParameter( vec3 c )
{
	return c * ( c * ( c * 0.305306017 + 0.682171106 ) + 0.0125228781 );
}

vec3 FmaOverlay( vec3 base, vec3 blend )
{
	return mix( 2.0 * base * blend, 1.0 - 2.0 * ( 1.0 - base ) * ( 1.0 - blend ), step( vec3( 0.5 ), base ) );
}

vec3 FmaBodyColor( vec4 diffuse, vec2 uv, vec3 N, vec3 L, vec3 E, vec3 grid, float castShadow )
{
	const vec4 shadow = texture( detail_map_texture, uv );
	const vec4 mask = texture( mask_texture, uv );
	const vec4 vc = texture( tint_texture, uv );
	const vec3 lit = diffuse.rgb * grid;
	const vec3 shade = shadow.rgb * grid;
	const float maskBias = mask.g * 2.0 - 1.0;
	// r_fixFmaShadows: the grid light's cast shadow scales the half-Lambert term (1 without it), as toonShadow
	const float x = ( dot( N, L ) * 0.5 + 0.5 ) * castShadow * vc.a + maskBias + u_global.fmaBody.x;
	const vec2 edge = clamp( ( vec2( x ) - vec2( 0.495, 0.475 ) ) * vec2( 200.0, 40.0 ), 0.0, 1.0 );
	// Source ramp is quadratic, followed by the hard >= .5 branch (not a simple linear AA ramp).
	const float t = min( 1.0 - ( 1.0 - edge.x ) * ( 1.0 - edge.x ) + step( 0.5, x ), 1.0 );
	// R/G preclassify the exact source alpha gates; BC7 alpha 0.5 itself can decode on either side of 0.5.
	const float gate = step( 0.5, vc.r );
	const float hasAlpha = step( 0.0001, diffuse.a );
	const float nv = dot( N, E );
	// T=N fallback only for the missing source smooth normal; the view-normal spec term uses N as authored.
	const float tv = nv * 0.5 + 0.5;
	const float dodgeRim = FmaRimCurve( clamp( ( tv - 0.6 ) * 20.0, 0.0, 1.0 ) );
	const float burnRim = FmaRimCurve( clamp( ( tv - ( 1.0 - u_global.fmaDodge.w ) )
		* ( 1.0 + u_global.fmaBurn.w ) / max( u_global.fmaDodge.w, 1e-5 ), 0.0, 1.0 ) );
	const float visibleDodge = dodgeRim * hasAlpha;
	vec3 c = mix( shade, lit, clamp( gate * ( t * gate + visibleDodge ), 0.0, 1.0 ) );
	const float saturation = max( 1.0 + ( edge.y - t ) * dot( lit - shade, vec3( 1.7 ) ) * u_global.fmaBody.y, 1.0 );
	const float luminance = dot( c, vec3( 0.299, 0.587, 0.114 ) );
	c = ( c - luminance ) * saturation + luminance;
	const vec3 accent = mix( diffuse.rgb, vec3( 1.0, 1.0, 0.7 ), 0.5 );
	c += accent * ( diffuse.a * diffuse.a * dodgeRim * gate * 0.3 );
	float back = t * gate + step( 1.6, nv + 1.0 ) - hasAlpha + 1.0;
	back = 1.0 - back * step( -0.75, x + maskBias );
	back = ( 1.0 - back * mask.a ) * step( 0.5, vc.g );
	back *= x + gate - 1.0 >= -0.3 ? 1.0 : 1.0 - clamp( u_global.fmaBody.z, 0.0, 1.0 );
	c *= vec3( 0.8, 0.75, 0.8 ) + clamp( back, 0.0, 1.0 ) * vec3( 0.2, 0.25, 0.2 );
	const float dodge = visibleDodge * ( ( 1.0 - t ) * ( 1.0 - mask.a ) + mask.a );
	c = clamp( c / max( 1.0 - FmaColorParameter( u_global.fmaDodge.rgb ) * dodge, vec3( 1e-5 ) ), 0.0, 1.0 );
	const float burn = clamp( shadow.a * burnRim - diffuse.a * dodgeRim * gate, 0.0, 1.0 );
	c *= mix( vec3( 1.0 ), FmaColorParameter( u_global.fmaBurn.rgb ), burn );
	const float specAt = 1.0 - mask.b * 0.25;
	const float specX = clamp( ( x * 0.1 + ( nv + 1.0 ) * 0.45 - specAt ) / ( clamp( specAt - 0.002, 0.0, 1.0 ) - specAt ), 0.0, 1.0 );
	const float spec = 1.0 - specX * specX * ( 3.0 - 2.0 * specX );
	c += accent * ( 0.6 * mask.r * spec ) * pow( max( u_global.fmaSpec.rgb, 0.0 ), vec3( 2.2 ) );
	c = clamp( c, -1.0, 1.1 );
	if ( u_global.fmaRight.w > 0.5 ) {
		const vec3 decal = texture( detail_normal_texture, uv ).rgb;
		c *= decal * ( 1.0 - 0.2 * diffuse.rgb ) + 0.2 * diffuse.rgb;
	}
	// r_fmaDebug (charReviewFix bits 9-11): the body ramp's inputs as grey
	const uint fmaDbg = ( uint( u_camera.charReviewFix ) >> 9u ) & 7u;
	if ( fmaDbg == 1u ) return vec3( clamp( x, 0.0, 1.0 ) );
	if ( fmaDbg == 2u ) return vec3( maskBias * 0.5 + 0.5 );
	if ( fmaDbg == 3u ) return vec3( vc.a );
	if ( fmaDbg == 4u ) return vec3( dot( N, L ) * 0.5 + 0.5 );
	if ( fmaDbg == 5u ) return vec3( t );
	if ( fmaDbg == 6u ) return vec3( spec * mask.r );
	return c * clamp( c - 0.07, 0.0, 1.0 );
}

vec3 FmaFaceColor( vec4 diffuse, vec2 uv, vec3 N, vec3 L, vec3 E, vec3 grid, float shadow )
{
	const vec4 vc = texture( tint_texture, uv );
	const float forward = dot( L, normalize( u_global.fmaForward.xyz ) );
	const float right = dot( L, normalize( u_global.fmaRight.xyz ) );
	// Source script setter was not recovered: normalized entity-space azimuth is the declared port convention.
	const float angle = dot( vec2( forward, right ), vec2( forward, right ) ) < 1e-8 ? 0.5 : atan( right, forward ) / ( 2.0 * M_PI ) + 0.5;
	const vec2 sdf = texture( emissive_texture, uv ).rg;
	const float d = angle > 0.5 ? sdf.r - angle : -sdf.g + 1.0 - angle;
	// r_fixFmaShadows 2: a shadowed point reads the SDF as turned away (1 without a shadow; 1 = body only)
	const float value = step( 0.5, vc.r ) * ( 2.0 * abs( abs( d ) - 0.5 ) ) * ( ReviewFix( 256u ) ? shadow : 1.0 );
	const float lit = step( 0.5, value );
	vec3 c = diffuse.rgb * clamp( diffuse.rgb - 0.07, 0.0, 1.0 );
	const float sssWeight = diffuse.a * clamp( diffuse.a - 0.07, 0.0, 1.0 );
	// DDS retains exported PNG rows: Unity v is flipped in the GLM convention.
	c = clamp( c * texture( layer_mask_texture, vec2( lit, 1.0 - vc.b ) ).rgb * grid, 0.0, 1.0 );
	const vec3 sss = texture( physical_texture, vec2( value, 1.0 - vc.b ) ).rgb;
	c = FmaOverlay( c, ( sss - 0.5 ) * sssWeight + 0.5 );
	// Source rim coordinate is view-facing normalize(.6*N + .4*T); T=N fallback leaves its direction N.
	const float viewHalf = dot( N, E ) * 0.5 + 0.5;
	const float hl = dot( N, L ) * 0.5 + 0.5;
	const float rimU = 1.0 - viewHalf * viewHalf * viewHalf * viewHalf;
	const vec3 rim = texture( normal_texture, vec2( rimU, 1.0 - vc.b ) ).rgb;
	c = 1.0 - ( 1.0 - c ) * ( 1.0 - rim * sssWeight * ( hl * 0.5 + 0.5 ) );
	return c * mix( vec3( 0.61, 0.55, 0.61 ), vec3( 1.0 ), step( 0.5, vc.g ) );
}

vec3 FmaColor( vec4 diffuse, vec2 uv, vec3 N, vec3 L, vec3 E, vec3 directed, vec3 ambient, float shadow )
{
	// Same single main grid-light and palette-preserving exposure convention as toonMain. FMA owns its
	// Gamma texture/contrast math; r_charLinear's PBR texture conversion and tone mapper do not run a second time.
	vec3 grid = ( directed + ambient ) * u_camera.sunAmbCol.w;
	grid = mix( vec3( dot( grid, vec3( 0.2126, 0.7152, 0.0722 ) ) ), grid, u_global.toonLight.x );
	// the light through ToonExposure's knee first, keeping its colour share, so the
	// source's clamps below see light in the range the game produced (a bright spot no longer clips the face to white)
	{
		const float level = dot( grid, vec3( 0.2126, 0.7152, 0.0722 ) ), knee = u_global.toonLight.y, over = level - knee;
		if ( over > 0.0 )
			grid *= ( knee + over / ( 1.0 + over / max( u_global.toonLight.z, 1e-3 ) ) ) / level;
	}
	vec3 c = ( material_mode & 32 ) != 0 ? FmaBodyColor( diffuse, uv, N, L, E, grid, shadow ) : FmaFaceColor( diffuse, uv, N, L, E, grid, shadow );
	// MultiCamera/Composite only composites. MultiCamera/Copy conditionally sqrt-encodes the actor RT.
	// fmaPost=1 chooses that display path; fmaPost=0 exposes the raw source RT. Active runtime flag is unqualified.
	if ( u_global.fmaBody.w > 0.5 )
		c = sqrt( max( c, 0.0 ) );
	return ToonExposure( c, diffuse.rgb );
}
