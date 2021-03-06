/*
 -----------------------------------------------------------------------------
 This source file is part of fastbird engine
 For the latest info, see http://www.jungwan.net/
 
 Copyright (c) 2013-2015 Jungwan Byun
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 -----------------------------------------------------------------------------
*/

#include "stdafx.h"
#include "ParticleOptions.h"
#include "ParticleSystem.h"
#include "FBConsole/Console.h"
using namespace fb;

static void EditParticle(StringVector& arg);
static void ScaleEditingParticle(StringVector& arg);

FB_IMPLEMENT_STATIC_CREATE(ParticleOptions)
ParticleOptions::ParticleOptions(){
	MoveEditParticle = 0;
	r_ParticleProfile = 0;
	FB_REGISTER_CVAR(MoveEditParticle, MoveEditParticle, CVAR_CATEGORY_CLIENT, "MoveEditParticle");
	FB_REGISTER_CVAR(r_ParticleProfile, r_ParticleProfile, CVAR_CATEGORY_CLIENT, "particle profiler");
	FB_REGISTER_CC(EditParticle, "Start editing particle");
	FB_REGISTER_CC(ScaleEditingParticle, "Scale editing particle");	
}

ParticleOptions::~ParticleOptions(){

}

void EditParticle(StringVector& arg)
{
	if (arg.size() < 2)
		return;

	ParticleSystem::GetInstance().EditThisParticle(arg[1].c_str());
}

void ScaleEditingParticle(StringVector& arg)
{
	if (arg.size() < 2)
		return;
	ParticleSystem::GetInstance().ScaleEditingParticle(StringConverter::ParseReal(arg[1]));
}