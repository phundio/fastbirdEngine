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

#pragma once

#include "Container.h"
#include "ImageDisplay.h"

namespace fb
{
FB_DECLARE_SMART_PTR(RenderTarget);
FB_DECLARE_SMART_PTR_STRUCT(TextureAtlasRegion);
FB_DECLARE_SMART_PTR(TextureAtlas);
FB_DECLARE_SMART_PTR(ImageBox);
class FB_DLL_UI ImageBox : public Container
{
protected:
	ImageBox();
	~ImageBox();

public:
	static ImageBoxPtr Create();
	void OnCreated();
	
	// IWinBase
	void OnResolutionChanged(HWindowId hwndId);
	ComponentType::Enum GetType() const { return ComponentType::ImageBox; }
	void GatherVisit(std::vector<UIObject*>& v);
	void OnSizeChanged();
	void OnParentSizeChanged();
	void OnAnySizeChanged();
	void OnStartUpdate(float elapsedTime);
	bool OnInputFromHandler(IInputInjectorPtr injector);

	// ImageBox;
	void SetTexture(const char* file);
	void SetTexture(TexturePtr pTexture);
	const Vec2I GetTextureSize(bool *outIsAtlas, Vec2 quadUV[4]) const;
	// image box will own the rt.
	void SetRenderTargetTexture(RenderTargetPtr rt);
	RenderTargetPtr GetRenderTargetTexture() const { return mRenderTarget; }
	void SetEnableRenderTarget(bool enable);
	void SetUseHighlight(bool use) { mUseHighlight = use; }
	// or
	const Vec2I& SetTextureAtlasRegion(const char* atlas, const char* region);
	void SetTextureAtlasRegions(const char* atlas, const std::vector<std::string>& data);
	void ChangeRegion(TextureAtlasRegionPtr region);
	void ChangeRegion(const char* region);
	TextureAtlasRegionPtr GetTextureAtlasRegion() const { return mAtlasRegion; }
	bool IsAnimated() const;
	void Highlight(bool enable);
	void SetImageDisplay(ImageDisplay::Enum display);
	bool SetProperty(UIProperty::Enum prop, const char* val);
	bool GetProperty(UIProperty::Enum prop, char val[], unsigned bufsize, bool notDefaultOnly);
	void SetVisibleInternal(bool visible);

	void SetUVRot(bool set);
	void SetCenterUVMatParam();
	void MatchUISizeToImageCenteredAt(const Vec2I& wpos);
	void MatchUISizeToImageAtCenter();
	void MatchUISizeToImage();
	void SetDesaturate(bool desat);
	void SetAmbientColor(const Vec4& color);
	// color multiplier
	void SetSpecularColor(const Vec4& color);
	void SetDefaultImageAtlasPathIfNotSet();
	void SetBorderAlpha(bool use);	

private:

	ImageBoxPtr ImageBox::CreateChildImageBox();
	void CalcUV();

	void SetAlphaTextureAutoGenerated(bool set);
	void KeepImageRatioMatchWidth(float imgRatio, float uiRatio, bool textureAtlas, Vec2 const defaultUV[4]);
	void KeepImageRatioMatchHeight(float imgRatio, float uiRatio, bool textureAtlas, Vec2 const defaultUV[4]);


private:
	std::string mTextureAtlasFile;
	std::string mStrRegion;
	std::string mStrRegions;
	std::string mStrFrameImage;
	std::string mImageFile; 
	TextureAtlasPtr mTextureAtlas;
	TextureAtlasRegionPtr mAtlasRegion;
	std::vector<TextureAtlasRegionPtr> mAtlasRegions;
	RenderTargetPtr mRenderTarget;
	// should not be smart pointer
	// material will hold a reference of this image.
	TexturePtr mTexture;
	ImageDisplay::Enum mImageDisplay;
	bool mUseHighlight;
	ImageBoxPtr mFrameImage;	
	float mSecPerFrame;
	float mPlayingTime;
	unsigned mCurFrame;
	bool mAnimation;
	bool mColorOveraySet;
	bool mImageRot;
	bool mLinearSampler;
	bool mSeperatedBackground;
};
}