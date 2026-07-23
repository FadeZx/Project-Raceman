#version 450 core

// SMAA pass 2/3: blending weight calculation (orthogonal patterns + corner rounding).
// Ported from the reference implementation (Jimenez et al., http://www.iryoku.com/smaa/).

layout(location = 0) out vec4 FragWeights;
in vec2 vUV;

uniform sampler2D uEdgesTexture;   // linear filtering required
uniform sampler2D uAreaTexture;    // 160x560 RG8, linear
uniform sampler2D uSearchTexture;  // 64x16 R8, nearest
uniform vec4 uRtMetrics;           // 1/w, 1/h, w, h

// Ultra preset search ranges.
const int SMAA_MAX_SEARCH_STEPS = 32;
const int SMAA_MAX_SEARCH_STEPS_DIAG = 16;
const float SMAA_AREATEX_MAX_DISTANCE = 16.0;
const float SMAA_AREATEX_MAX_DISTANCE_DIAG = 20.0;
const vec2 SMAA_AREATEX_PIXEL_SIZE = 1.0 / vec2(160.0, 560.0);
const float SMAA_AREATEX_SUBTEX_SIZE = 1.0 / 7.0;
const vec2 SMAA_SEARCHTEX_SIZE = vec2(66.0, 33.0);
const vec2 SMAA_SEARCHTEX_PACKED_SIZE = vec2(64.0, 16.0);
const float SMAA_CORNER_ROUNDING_NORM = 25.0 / 100.0;

// The search texture packs, for each pair of bilinear-fetched edge values, how
// many pixels the search can safely jump.
float SearchLength(vec2 e, float offset) {
    vec2 scale = SMAA_SEARCHTEX_SIZE * vec2(0.5, -1.0);
    vec2 bias = SMAA_SEARCHTEX_SIZE * vec2(offset, 1.0);
    scale += vec2(-1.0, 1.0);
    bias += vec2(0.5, -0.5);
    scale *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;
    bias *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;
    return textureLod(uSearchTexture, scale * e + bias, 0.0).r;
}

float SearchXLeft(vec2 texcoord, float end) {
    vec2 e = vec2(0.0, 1.0);
    while (texcoord.x > end && e.g > 0.8281 && e.r == 0.0) {
        e = textureLod(uEdgesTexture, texcoord, 0.0).rg;
        texcoord = -vec2(2.0, 0.0) * uRtMetrics.xy + texcoord;
    }
    float offset = -(255.0 / 127.0) * SearchLength(e, 0.0) + 3.25;
    return uRtMetrics.x * offset + texcoord.x;
}

float SearchXRight(vec2 texcoord, float end) {
    vec2 e = vec2(0.0, 1.0);
    while (texcoord.x < end && e.g > 0.8281 && e.r == 0.0) {
        e = textureLod(uEdgesTexture, texcoord, 0.0).rg;
        texcoord = vec2(2.0, 0.0) * uRtMetrics.xy + texcoord;
    }
    float offset = -(255.0 / 127.0) * SearchLength(e, 0.5) + 3.25;
    return -uRtMetrics.x * offset + texcoord.x;
}

float SearchYUp(vec2 texcoord, float end) {
    vec2 e = vec2(1.0, 0.0);
    while (texcoord.y > end && e.r > 0.8281 && e.g == 0.0) {
        e = textureLod(uEdgesTexture, texcoord, 0.0).rg;
        texcoord = -vec2(0.0, 2.0) * uRtMetrics.xy + texcoord;
    }
    float offset = -(255.0 / 127.0) * SearchLength(e.gr, 0.0) + 3.25;
    return uRtMetrics.y * offset + texcoord.y;
}

float SearchYDown(vec2 texcoord, float end) {
    vec2 e = vec2(1.0, 0.0);
    while (texcoord.y < end && e.r > 0.8281 && e.g == 0.0) {
        e = textureLod(uEdgesTexture, texcoord, 0.0).rg;
        texcoord = vec2(0.0, 2.0) * uRtMetrics.xy + texcoord;
    }
    float offset = -(255.0 / 127.0) * SearchLength(e.gr, 0.5) + 3.25;
    return -uRtMetrics.y * offset + texcoord.y;
}

vec2 SampleEdgesOffset(vec2 texcoord, vec2 pixelOffset) {
    return textureLod(uEdgesTexture, texcoord + pixelOffset * uRtMetrics.xy, 0.0).rg;
}

// --- Diagonal pattern detection ---------------------------------------------

// Undo the bilinear packing of two adjacent edge texels fetched between pixels.
vec2 DecodeDiagBilinearAccess(vec2 e) {
    e.r = e.r * abs(5.0 * e.r - 5.0 * 0.75);
    return round(e);
}

vec4 DecodeDiagBilinearAccess(vec4 e) {
    e.rb = e.rb * abs(5.0 * e.rb - 5.0 * 0.75);
    return round(e);
}

vec2 SearchDiag1(vec2 texcoord, vec2 dir, out vec2 e) {
    vec4 coord = vec4(texcoord, -1.0, 1.0);
    vec3 t = vec3(uRtMetrics.xy, 1.0);
    e = vec2(0.0);
    while (coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) && coord.w > 0.9) {
        coord.xyz = t * vec3(dir, 1.0) + coord.xyz;
        e = textureLod(uEdgesTexture, coord.xy, 0.0).rg;
        coord.w = dot(e, vec2(0.5));
    }
    return coord.zw;
}

vec2 SearchDiag2(vec2 texcoord, vec2 dir, out vec2 e) {
    vec4 coord = vec4(texcoord, -1.0, 1.0);
    coord.x += 0.25 * uRtMetrics.x;
    vec3 t = vec3(uRtMetrics.xy, 1.0);
    e = vec2(0.0);
    while (coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) && coord.w > 0.9) {
        coord.xyz = t * vec3(dir, 1.0) + coord.xyz;
        e = textureLod(uEdgesTexture, coord.xy, 0.0).rg;
        e = DecodeDiagBilinearAccess(e);
        coord.w = dot(e, vec2(0.5));
    }
    return coord.zw;
}

vec2 AreaDiag(vec2 dist, vec2 e) {
    vec2 texcoord = vec2(SMAA_AREATEX_MAX_DISTANCE_DIAG) * e + dist;
    texcoord = SMAA_AREATEX_PIXEL_SIZE * texcoord + 0.5 * SMAA_AREATEX_PIXEL_SIZE;
    texcoord.x += 0.5; // Diagonal areas live in the right half of the area texture.
    return textureLod(uAreaTexture, texcoord, 0.0).rg;
}

vec2 CalculateDiagWeights(vec2 texcoord, vec2 e) {
    vec2 weights = vec2(0.0);

    vec4 d;
    vec2 end;
    if (e.r > 0.0) {
        d.xz = SearchDiag1(texcoord, vec2(-1.0, 1.0), end);
        d.x += float(end.y > 0.9);
    } else {
        d.xz = vec2(0.0);
    }
    d.yw = SearchDiag1(texcoord, vec2(1.0, -1.0), end);

    if (d.x + d.y > 2.0) { // d.x + d.y + 1 > 3
        vec4 coords = vec4(-d.x + 0.25, d.x, d.y, -d.y - 0.25) * uRtMetrics.xyxy + texcoord.xyxy;
        vec4 c;
        c.xy = SampleEdgesOffset(coords.xy, vec2(-1.0, 0.0));
        c.zw = SampleEdgesOffset(coords.zw, vec2(1.0, 0.0));
        c.yxwz = DecodeDiagBilinearAccess(c.xyzw);

        vec2 cc = vec2(2.0) * c.xz + c.yw;
        cc = mix(cc, vec2(0.0), step(0.9, d.zw));
        weights += AreaDiag(d.xy, cc);
    }

    d.xz = SearchDiag2(texcoord, vec2(-1.0, -1.0), end);
    if (SampleEdgesOffset(texcoord, vec2(1.0, 0.0)).r > 0.0) {
        d.yw = SearchDiag2(texcoord, vec2(1.0, 1.0), end);
        d.y += float(end.y > 0.9);
    } else {
        d.yw = vec2(0.0);
    }

    if (d.x + d.y > 2.0) {
        vec4 coords = vec4(-d.x, -d.x, d.y, d.y) * uRtMetrics.xyxy + texcoord.xyxy;
        vec4 c;
        c.x = SampleEdgesOffset(coords.xy, vec2(-1.0, 0.0)).g;
        c.y = SampleEdgesOffset(coords.xy, vec2(0.0, -1.0)).r;
        c.zw = SampleEdgesOffset(coords.zw, vec2(1.0, 0.0)).gr;
        vec2 cc = vec2(2.0) * c.xz + c.yw;
        cc = mix(cc, vec2(0.0), step(0.9, d.zw));
        weights += AreaDiag(d.xy, cc).gr;
    }

    return weights;
}

// --- Orthogonal pattern detection --------------------------------------------

vec2 Area(vec2 dist, float e1, float e2) {
    vec2 texcoord = vec2(SMAA_AREATEX_MAX_DISTANCE) * round(4.0 * vec2(e1, e2)) + dist;
    texcoord = SMAA_AREATEX_PIXEL_SIZE * texcoord + 0.5 * SMAA_AREATEX_PIXEL_SIZE;
    return textureLod(uAreaTexture, texcoord, 0.0).rg;
}

void DetectHorizontalCornerPattern(inout vec2 weights, vec4 texcoord, vec2 d) {
    vec2 leftRight = step(d.xy, d.yx);
    vec2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;
    rounding /= leftRight.x + leftRight.y;
    vec2 factor = vec2(1.0);
    factor.x -= rounding.x * SampleEdgesOffset(texcoord.xy, vec2(0.0, 1.0)).r;
    factor.x -= rounding.y * SampleEdgesOffset(texcoord.zw, vec2(1.0, 1.0)).r;
    factor.y -= rounding.x * SampleEdgesOffset(texcoord.xy, vec2(0.0, -2.0)).r;
    factor.y -= rounding.y * SampleEdgesOffset(texcoord.zw, vec2(1.0, -2.0)).r;
    weights *= clamp(factor, 0.0, 1.0);
}

void DetectVerticalCornerPattern(inout vec2 weights, vec4 texcoord, vec2 d) {
    vec2 leftRight = step(d.xy, d.yx);
    vec2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;
    rounding /= leftRight.x + leftRight.y;
    vec2 factor = vec2(1.0);
    factor.x -= rounding.x * SampleEdgesOffset(texcoord.xy, vec2(1.0, 0.0)).g;
    factor.x -= rounding.y * SampleEdgesOffset(texcoord.zw, vec2(1.0, 1.0)).g;
    factor.y -= rounding.x * SampleEdgesOffset(texcoord.xy, vec2(-2.0, 0.0)).g;
    factor.y -= rounding.y * SampleEdgesOffset(texcoord.zw, vec2(-2.0, 1.0)).g;
    weights *= clamp(factor, 0.0, 1.0);
}

void main() {
    vec2 pixcoord = vUV * uRtMetrics.zw;
    vec4 offset0 = uRtMetrics.xyxy * vec4(-0.25, -0.125, 1.25, -0.125) + vUV.xyxy;
    vec4 offset1 = uRtMetrics.xyxy * vec4(-0.125, -0.25, -0.125, 1.25) + vUV.xyxy;
    vec4 offset2 = uRtMetrics.xxyy * (vec4(-2.0, 2.0, -2.0, 2.0) * float(SMAA_MAX_SEARCH_STEPS)) +
        vec4(offset0.xz, offset1.yw);

    vec4 weights = vec4(0.0);
    vec2 e = texture(uEdgesTexture, vUV).rg;

    if (e.g > 0.0) { // Edge above: blend vertically.
        // Diagonal patterns take priority; the orthogonal search only runs
        // when no diagonal was found at this pixel.
        weights.rg = CalculateDiagWeights(vUV, e);
        if (weights.r + weights.g == 0.0) {
        vec2 d;
        vec3 coords;
        coords.x = SearchXLeft(offset0.xy, offset2.x);
        coords.y = offset1.y;
        d.x = coords.x;
        float e1 = textureLod(uEdgesTexture, coords.xy, 0.0).r;
        coords.z = SearchXRight(offset0.zw, offset2.y);
        d.y = coords.z;
        d = abs(round(uRtMetrics.zz * d - pixcoord.xx));
        vec2 sqrtD = sqrt(d);
        float e2 = SampleEdgesOffset(coords.zy, vec2(1.0, 0.0)).r;
        weights.rg = Area(sqrtD, e1, e2);
        coords.y = vUV.y;
        DetectHorizontalCornerPattern(weights.rg, coords.xyzy, d);
        } else {
            e.r = 0.0; // A diagonal was handled; skip the vertical-edge pass.
        }
    }

    if (e.r > 0.0) { // Edge on the left: blend horizontally.
        vec2 d;
        vec3 coords;
        coords.y = SearchYUp(offset1.xy, offset2.z);
        coords.x = offset0.x;
        d.x = coords.y;
        float e1 = textureLod(uEdgesTexture, coords.xy, 0.0).g;
        coords.z = SearchYDown(offset1.zw, offset2.w);
        d.y = coords.z;
        d = abs(round(uRtMetrics.ww * d - pixcoord.yy));
        vec2 sqrtD = sqrt(d);
        float e2 = SampleEdgesOffset(coords.xz, vec2(0.0, 1.0)).g;
        weights.ba = Area(sqrtD, e1, e2);
        coords.x = vUV.x;
        DetectVerticalCornerPattern(weights.ba, coords.xyxz, d);
    }

    FragWeights = weights;
}
