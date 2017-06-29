#include "config_bullet.h"
btVector3 BulletConfig::gravity = btVector3(0,0,-1);
float BulletConfig::dt = .01;
int BulletConfig::maxSubSteps = 2;
float BulletConfig::internalTimeStep = 1/200.;
float BulletConfig::friction = .5;
float BulletConfig::restitution = 0;
float BulletConfig::margin = .0005;
float BulletConfig::linkPadding = 0;
float BulletConfig::timeStep = 0.03;
float BulletConfig::fixedTimeStep = 0.015;
