#ifndef SHADER_PBR_GLSL
#define SHADER_PBR_GLSL

#if defined(PER_PIXEL_LIGHTING)
	float WrapLambert( in float NL, in float w )
	{
		return clamp((NL + w) / pow(1.0 + w, 2.0), 0.0, 1.0);
	}

	vec3 Diffuse_Lambert(in vec3 DiffuseColor)
	{
		return DiffuseColor * (1.0 / M_PI);
	}

	// normalTexel, layerMask and maskAlpha are main's reads of the normal map, the layer mask and the
	// material mask's alpha (the same texels this read again)
	vec3 CalcNormal( in vec3 vertexNormal, in vec4 vertexTangent, in vec2 frag_tex_coord, in vec4 normalTexel, in vec4 layerMask,
		in float maskAlpha )
	{
		if ( normal_texture_set > -1 || layer_count > 0 ) {
			vec3 biTangent = vertexTangent.w * cross(vertexNormal, vertexTangent.xyz);
			vec3 n = normal_texture_set > -1 ? normalTexel.agb - vec3(0.5) : vec3(0.0, 0.0, 0.5);

			n.xy *= u_global.normalScale.xy;
			// layers: each tiling layer's normal adds its x/y to the stage's by its mask weight (same half scale), as layer
			// detail on top of the surface's own shape (CP's multilayer slopes add to its global normal); a stage whose
			// normal map already holds a layer's detail should drop it there
			if ( layer_count > 0 ) {
				for ( int i = 0; i < layer_count; i++ ) {
					const vec2 ln = ( texture(layer_normal_texture, vec3(frag_tex_coord * u_global.layerParams[i].x, float(i))).rg - vec2(0.5) )
						* u_global.layerParams[i].y;
					n.xy += ln * ( layerMask[i] * u_global.layerTint[i].a );
				}
			}
			// detailNormalMap: the tiling map's x/y (same half scale) added before z is rebuilt, weighted by the
			// materialMask's alpha
			if ( detail_normal_set > -1 )
				n.xy += ( texture(detail_normal_texture, frag_tex_coord * u_global.detailParams.x).ag - vec2(0.5) ) * u_global.detailParams.y
					* ( mask_set > -1 ? maskAlpha : 1.0 );
			// the back of a two-sided surface flips N and with it the bitangent, not the
			// tangent; negating x makes the result the front's normal negated, so the weave is not mirrored
			if ( g_backFace )
				n.x = -n.x;
			n.z = sqrt(clamp((0.25 - n.x * n.x) - n.y * n.y, 0.0, 1.0));
			n = n.x * vertexTangent.xyz + n.y * biTangent + n.z * vertexNormal;

			return normalize(n);
		}
	
		else
			return normalize(vertexNormal);
	}

	vec3 CalcDiffuse( in vec3 diffuse, in float NE, in float NL,
		in float LH, in float roughness )
	{
		return Diffuse_Lambert(diffuse);
	}

	vec3 F_Schlick( in vec3 SpecularColor, in float VH )
	{
		float Fc = pow(1 - VH, 5);
		return clamp(50.0 * SpecularColor.g, 0.0, 1.0) * Fc + (1 - Fc) * SpecularColor; //hacky way to decide if reflectivity is too low (< 2%)
	}

	float D_GGX( in float NH, in float a )
	{
		float a2 = a * a;
		float d = (NH * a2 - NH) * NH + 1;
		return a2 / (M_PI * d * d);
	}

	// Appoximation of joint Smith term for GGX
	// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
	float V_SmithJointApprox( in float a, in float NV, in float NL )
	{
		float Vis_SmithV = NL * (NV * (1 - a) + a);
		float Vis_SmithL = NV * (NL * (1 - a) + a);
		return 0.5 * (1.0 / (Vis_SmithV + Vis_SmithL));
	}

	// Roughness convention: every shading function takes PERCEPTUAL roughness r (UE, glTF, Filament). The GGX terms
	// use alpha = r^2, at least 0.01 (r 0.1, the sharpest highlight the renderer ever drew: a point light's peak stays
	// finite); the probe reflections take r itself (mip lod and the BRDF LUT). Stages whose maps store another
	// encoding say so with roughnessEncoding (gen_frag roughness_encoding).
	float RoughnessToAlpha( in float r )
	{
		return max( r * r, 0.01 );
	}

	vec3 CalcSpecular( in vec3 specular, in float NH, in float NL,
		in float NE, float LH, in float VH, in float roughness )
	{
		const float a = RoughnessToAlpha( roughness );
		vec3  F = F_Schlick(specular, VH);
		float D = D_GGX(NH, a);
		float V = V_SmithJointApprox(a, NE, NL);

		return D * F * V;
	}

	// Reflection probe lookup direction for slot i. Parallax correction intersects the reflected ray from P with a proxy
	// of the room and samples the probe toward the hit, so reflections stay fixed to the walls:
	// r_cubeMapParallax 1: the box fitted around the probe at load (the usual parallax-corrected cubemap, Lagarde 2012),
	// faded back to the plain reflection within 32 units outside it; 2: the probe's sphere (misc_cubemap "radius"),
	// faded over its outer fifth. strength (0..1) scales the correction: rough surfaces skip it. The cubemaps are
	// stored with y flipped.
	vec3 ProbeDirection( in vec3 P, in vec3 R, in int i, in float strength )
	{
		vec3 D = R;
		const vec4 probe = u_global.cubeProbes[i];
		if ( strength <= 0.0 ) {
			// plain reflection
		} else if ( u_global.cubeParams.x > 1.5 ) {
			const vec3 toP = P - probe.xyz;
			const float r2 = probe.w * probe.w;
			const float c = dot( toP, toP ) - r2;	// < 0 inside the sphere
			if ( c < 0.0 ) {
				const float b = dot( toP, R );
				const vec3 hit = toP + ( sqrt( b * b - c ) - b ) * R;
				const float k = strength * clamp( -c / ( 0.36 * r2 ), 0.0, 1.0 );	// 1 within 0.8 radius
				D = normalize( mix( R, normalize( hit ), k ) );
			}
		} else if ( u_global.cubeParams.x > 0.5 ) {
			const vec3 bmin = u_global.cubeBoxMins[i].xyz, bmax = u_global.cubeBoxMaxs[i].xyz;
			const float outside = length( max( bmin - P, 0.0 ) + max( P - bmax, 0.0 ) );
			const float k = strength * ( 1.0 - clamp( outside / 32.0, 0.0, 1.0 ) );
			if ( k > 0.0 ) {
				const vec3 Rs = R + vec3( 1e-6 );
				const vec3 tf = max( ( bmax - P ) / Rs, ( bmin - P ) / Rs );
				const float t = min( min( tf.x, tf.y ), tf.z );
				if ( t > 0.0 )
					D = normalize( mix( R, normalize( P + t * R - probe.xyz ), k ) );
			}
		}
		D.y = -D.y;
		return D;
	}

	// P: the shaded point in world space. env_texture[i] is the draw's probe (0) and its nearest neighbours, blended
	// by inverse distance to the fourth power: the nearest probe dominates and neighbours fade in toward the
	// midpoints. A slot with radius 0 is unused; with none in use (no probe) slot 0 is sampled alone, as before.
	// cut (0..1): every weight is lowered by that share of the largest and the rest renormalised, so neighbours fade
	// out smoothly (no seam) and are not sampled at all where they reach zero; the largest always stays.
	void ProbeWeights( in vec3 P, in float cut, out float w[VK_CUBEMAP_BLEND_PROBES] )
	{
		float wMax = 0.0;
		for ( int i = 0; i < VK_CUBEMAP_BLEND_PROBES; i++ ) {
			const vec3 v = P - u_global.cubeProbes[i].xyz;
			const float d2 = dot( v, v ) + 1.0;
			w[i] = u_global.cubeProbes[i].w > 0.0 ? 1.0 / ( d2 * d2 ) : 0.0;
			wMax = max( wMax, w[i] );
		}
		if ( wMax > 0.0 ) {
			for ( int i = 0; i < VK_CUBEMAP_BLEND_PROBES; i++ )
				w[i] = max( w[i] - cut * wMax, 0.0 );
		} else {
			w[0] = 1.0;
		}
	}

	// The work follows how sharp the reflection is (r_cubeMapRoughCut, default on): from roughness 0.35 to 0.7 the
	// parallax correction fades out and the probe cut rises from 1/256 to 0.6 of the largest weight, so rough
	// surfaces sample about one probe in the plain direction (the blurred mips cannot show more) and smooth ones get
	// the full blend and correction. roughness is perceptual: mip j of a probe was prefiltered with GGX at perceptual
	// roughness j / VK_CUBEMAP_ROUGH_MIPS (vk_cubemap.cpp), so lod = roughness x that count.
	vec3 SampleProbes( in vec3 P, in vec3 R, in float roughness )
	{
		const float rough = u_global.cubeParams.z > 0.5 ? smoothstep( 0.35, 0.7, roughness ) : 0.0;
		float w[VK_CUBEMAP_BLEND_PROBES];
		ProbeWeights( P, mix( 1.0 / 256.0, 0.6, rough ), w );

		const float lod = roughness * float( VK_CUBEMAP_ROUGH_MIPS );
		vec3 c = vec3( 0.0 );
		float used = 0.0;
		for ( int i = 0; i < VK_CUBEMAP_BLEND_PROBES; i++ ) {
			if ( w[i] > 0.0 ) {
				c += w[i] * textureLod( env_texture[i], ProbeDirection( P, R, i, 1.0 - rough ), lod ).rgb;
				used += w[i];
			}
		}
		return c / used;
	}

	// the probes' diffuse irradiance toward D (r_cubeMapDiffuse): the same blend as a rough reflection (mostly the
	// nearest probe, neighbours faded in toward the midpoints), no parallax
	vec3 SampleIrradiance( in vec3 P, in vec3 D )
	{
		float w[VK_CUBEMAP_BLEND_PROBES];
		ProbeWeights( P, 0.6, w );

		const vec3 Dy = vec3( D.x, -D.y, D.z );	// the cubemaps are stored with y flipped
		vec3 c = vec3( 0.0 );
		float used = 0.0;
		for ( int i = 0; i < VK_CUBEMAP_BLEND_PROBES; i++ ) {
			if ( w[i] > 0.0 ) {
				c += w[i] * textureLod( irradiance_texture[i], Dy, 0.0 ).rgb;
				used += w[i];
			}
		}
		return c / used;
	}

	vec3 CalcIBLContribution( in float roughness, in vec3 N, in vec3 E,
		in float NE, in vec3 specular, in vec3 P )
	{
		vec3 cubeLightColor;
		// r_charLinear 3 in a render scene: the scene's own sky and ground hemisphere along the reflected ray, after its
		// exposure, as its surfaces mirror it (the BSP's probes are drawn without the scene: a bare, bright world)
		if ( mod( u_entity.ambientLight.w, 10.0 ) > 2.5 && u_camera.charTone.x > 0.5 ) {
			const vec3 R = reflect( -E, N );
			cubeLightColor = mix( u_camera.charGround.rgb, u_camera.charSky.rgb, R.z * 0.5 + 0.5 );
		} else {
			cubeLightColor = SampleProbes( P, reflect( -E, N ), roughness );
			// characters lit in linear space (r_charLinear): the probes hold sRGB-encoded light (r_charTerms 8: added raw, as before)
			if ( mod( u_entity.ambientLight.w, 10.0 ) > 0.5 && ( int( u_entity.ambientLight.w * 0.1 + 0.05 ) & 8 ) == 0 )
				cubeLightColor = mix( cubeLightColor / 12.92, pow( ( cubeLightColor + 0.055 ) / 1.055, vec3( 2.4 ) ), step( vec3( 0.04045 ), cubeLightColor ) );
		}
		vec2 EnvBRDF = texture(brdflut_texture, vec2(NE, 1.0 - roughness)).rg;

		return cubeLightColor * (specular.rgb * EnvBRDF.x + EnvBRDF.y);
	}

	// r_cubeMapDebug 1: the probe weights (the draw's probe red, its neighbours from green to purple by distance);
	// 2: the surface as a flat mirror (vertex normal, sharpest mip, the same blend and parallax), which shows whether
	// reflections line up with the room and where neighbouring surfaces disagree; 3: as 1, each probe in its own colour
	vec3 CalcIBLDebug( in vec3 vertexNormal, in vec3 E, in vec3 P )
	{
		if ( DebugCubeMap() > 1.5 && DebugCubeMap() < 2.5 )
			return SampleProbes( P, reflect( -E, normalize( vertexNormal ) ), 0.0 );	// roughness 0: the full blend

		const vec3 slotColor[8] = vec3[8]( vec3( 1, 0, 0 ), vec3( 0, 1, 0 ), vec3( 0, 0, 1 ), vec3( 1, 1, 0 ),
			vec3( 0, 1, 1 ), vec3( 1, 0, 1 ), vec3( 1, 0.5, 0 ), vec3( 0.5, 0, 1 ) );
		float w[VK_CUBEMAP_BLEND_PROBES];
		ProbeWeights( P, 1.0 / 256.0, w );
		float wSum = 0.0;
		vec3 c = vec3( 0.0 );
		for ( int i = 0; i < VK_CUBEMAP_BLEND_PROBES; i++ ) {
			// 3: each probe's own colour (from its origin), the same on every surface
			const vec3 probeColor = fract( sin( u_global.cubeProbes[i].xyz * vec3( 0.0123, 0.0171, 0.0313 )
				+ u_global.cubeProbes[i].yzx * vec3( 0.0097, 0.0151, 0.0071 ) ) * 437.5453 );
			c += w[i] * ( DebugCubeMap() > 2.5 ? probeColor : slotColor[i & 7] );
			wSum += w[i];
		}
		return c / wSum;
	}

	#ifdef VK_DLIGHT_GPU
		float CalcLightAttenuation(float normDist)
		{
			// zero light at 1.0, approximating q3 style
			float attenuation = 0.5 * normDist - 0.5;
			return clamp(attenuation, 0.0, 1.0);
		}

		vec3 CalcDynamicLightContribution(
			in float roughness,
			in vec3 N,
			in vec3 E,
			in vec3 viewOrigin,
			in vec3 viewDir,
			in float NE,
			in vec3 diffuse,
			in vec3 specular,
			in vec3 vertexNormal )
		{
			vec3 outColor = vec3(0.0);
			vec3 position = viewOrigin - viewDir;

			for ( int i = 0; i < u_lights.num_lights; i++ )
			{
				#if 0
					if ( ( u_LightMask & ( 1 << i ) ) == 0 ) {
						continue;
					}
				#endif

				vkUniformLightEntry_t light = u_lights.light[i];

				vec3  L  = light.origin.xyz - position;
				float sqrLightDist = dot(L, L);

				// sunny, skip if light is out of radius
				float radiusSqr = light.radius * light.radius;
				if (sqrLightDist > radiusSqr) {
					continue;
				}

				float attenuation = CalcLightAttenuation(light.radius * light.radius / sqrLightDist);

				// sunny, skip lights with negligible intensity
				if (attenuation < 0.001) {
					continue;
				}

				L /= sqrt(sqrLightDist);

				vec3  H  = normalize(L + E);
				float NL = clamp(dot(N, L), 0.0, 1.0);
				float LH = clamp(dot(L, H), 0.0, 1.0);
				float NH = clamp(dot(N, H), 0.0, 1.0);
				float VH = clamp(dot(E, H), 0.0, 1.0);

				// the grid and material paths scale ( Fd + Fs ) by pi with a diffuse already / pi;
				// here the diffuse skips both, so the specular alone was pi times weak
				vec3 reflectance = diffuse + CalcSpecular(specular, NH, NL, NE, LH, VH, roughness) * M_PI;

				outColor += light.color * reflectance * attenuation * NL;
			}

			return outColor;
		}
	#endif
#endif

#endif