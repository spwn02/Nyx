#version 460 core

layout(location = 0) out uint oMask;

in VS_OUT {
  flat uint pickID;
} f;

void main() {
  oMask = f.pickID;
}
