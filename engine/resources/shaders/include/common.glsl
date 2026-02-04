float saturate(float x) { return clamp(x, 0.0, 1.0); }

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
  return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
  return F0 + (max(vec3(1.0 - roughness), F0) - F0) *
                  pow(1.0 - cosTheta, 5.0);
}

float D_GGX(float NoH, float a) {
  float a2 = a * a;
  float d = (NoH * NoH) * (a2 - 1.0) + 1.0;
  return a2 / (3.14159265 * d * d);
}

float V_SmithGGX(float NoV, float NoL, float a) {
  float a2 = a * a;
  float gv = NoL * sqrt(NoV * (NoV - NoV * a2) + a2);
  float gl = NoV * sqrt(NoL * (NoL - NoL * a2) + a2);
  return 0.5 / max(gv + gl, 1e-5);
}

vec3 BRDF(vec3 N, vec3 V, vec3 L, vec3 baseColor, float metallic,
          float roughness) {
  vec3 H = normalize(V + L);

  float NoV = saturate(dot(N, V));
  float NoL = saturate(dot(N, L));
  float NoH = saturate(dot(N, H));
  float VoH = saturate(dot(V, H));

  float a = max(0.04, roughness * roughness);

  vec3 F0 = mix(vec3(0.04), baseColor, metallic);
  vec3 F = fresnelSchlick(VoH, F0);

  float D = D_GGX(NoH, a);
  float Vg = V_SmithGGX(NoV, NoL, a);

  vec3 spec = (D * Vg) * F;
  vec3 kd = (1.0 - F) * (1.0 - metallic);
  vec3 diff = kd * baseColor * (1.0 / 3.14159265);

  return (diff + spec) * NoL;
}

vec3 computeIBL(vec3 N, vec3 V, vec3 baseColor, float metallic,
                float roughness, float ao, samplerCube envIrradiance,
                samplerCube envPrefilter, sampler2D brdfLut) {
  vec3 F0 = mix(vec3(0.04), baseColor, metallic);
  float NoV = saturate(dot(N, V));
  vec3 F = fresnelSchlickRoughness(NoV, F0, roughness);

  vec3 kS = F;
  vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

  vec3 irradiance = texture(envIrradiance, N).rgb;
  vec3 diffuseIBL = irradiance * baseColor;

  vec3 R = reflect(-V, N);
  float maxMip = float(textureQueryLevels(envPrefilter) - 1);
  vec3 prefiltered = textureLod(envPrefilter, R, roughness * maxMip).rgb;
  vec2 brdf = texture(brdfLut, vec2(NoV, roughness)).rg;
  vec3 specularIBL = prefiltered * (F * brdf.x + brdf.y);

  return (kD * diffuseIBL + specularIBL) * ao;
}
