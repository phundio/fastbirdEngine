#pragma once
#include <Engine/Renderer/IRenderState.h>
#include <d3d11.h>

namespace fastbird
{
	//-------------------------------------------------------------------------
	class RasterizerStateD3D11 : public IRasterizerState
	{
	public:
		RasterizerStateD3D11();
		virtual ~RasterizerStateD3D11();

		//--------------------------------------------------------------------
		// IRasterizerState Interfacec
		//--------------------------------------------------------------------
		virtual void Bind();

		//--------------------------------------------------------------------
		// OWN Interfacec
		//--------------------------------------------------------------------
		void SetHardwareRasterizerState(ID3D11RasterizerState* pRasterizerState);
		ID3D11RasterizerState* GetHardwareRasterizerState() const { return mRasterizerState; }

	private:
		ID3D11RasterizerState* mRasterizerState;
	};

	//-------------------------------------------------------------------------
	class BlendStateD3D11 : public IBlendState
	{
	public:
		BlendStateD3D11();
		virtual ~BlendStateD3D11();

		//--------------------------------------------------------------------
		// IBlendState Interfacec
		//--------------------------------------------------------------------
		virtual void Bind();

		//--------------------------------------------------------------------
		// OWN Interfacec
		//--------------------------------------------------------------------
		void SetHardwareBlendState(ID3D11BlendState* pBlendState);
		ID3D11BlendState* GetHardwareBlendState() const { return mBlendState; }
		float* GetBlendFactor() const { return 0; }
		DWORD GetBlendMask() const { return 0xffffffff;}

	private:
		ID3D11BlendState* mBlendState;
	};

	//-------------------------------------------------------------------------
	class DepthStencilStateD3D11 : public IDepthStencilState
	{
	public:
		DepthStencilStateD3D11();
		virtual ~DepthStencilStateD3D11();

		//--------------------------------------------------------------------
		// IDepthStencilState Interfacec
		//--------------------------------------------------------------------
		virtual void Bind(unsigned stencilRef);

		//--------------------------------------------------------------------
		// OWN Interfacec
		//--------------------------------------------------------------------
		void SetHardwareBlendState(ID3D11DepthStencilState* pDepthStencilState);
		ID3D11DepthStencilState* GetHardwareBlendState() const { return mDepthStencilState; }

	private:
		ID3D11DepthStencilState* mDepthStencilState;
	};
}