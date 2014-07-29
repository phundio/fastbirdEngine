#include <Engine/StdAfx.h>
#include <Engine/Renderer/Material.h>
#include <Engine/IShader.h>
#include <Engine/GlobalEnv.h>
#include <Engine/IEngine.h>
#include <Engine/IRenderer.h>
#include <Engine/ITexture.h>
#include <Engine/Renderer/Shader.h>
#include <Engine/Renderer/RendererStructs.h>
#include <FreeImage.h>

using namespace fastbird;

IMaterial* IMaterial::CreateMaterial(const char* file)
{
	if (gFBEnv && gFBEnv->pRenderer)
	{
		IMaterial* pMat = gFBEnv->pRenderer->CreateMaterial(file);
		assert(pMat);
		return pMat;
	}

	return 0;
}

void IMaterial::ReloadMaterial(const char* file)
{
	Material::ReloadMaterial(file);
}

void IMaterial::ReloadShader(const char* shader)
{
	Material::ReloadShader(shader);
}

//----------------------------------------------------------------------------
Material::Materials Material::mMaterials;
FB_READ_WRITE_CS Material::mRWCSMaterial;

//----------------------------------------------------------------------------
Material::Material()
	: mReloading(false)
	, mReloadingTryCount(0)
	, mAdamMaterial(0)
	, mShaders(0)
{
	WRITE_LOCK wl(mRWCSMaterial);
	mMaterials.push_back(this);
}

Material::Material(const Material& mat)
	: mReloading(false)
	, mReloadingTryCount(0)
	, mAdamMaterial(0)
{
	{
		WRITE_LOCK wl(mRWCSMaterial);
		mMaterials.push_back(this);
	}
	mMaterialConstants =	mat.mMaterialConstants;
	mShader =				mat.mShader;
	mName =					mat.mName;
	mMaterialParameters =	mat.mMaterialParameters;
	mColorRampMap =			mat.mColorRampMap;
	mShaderDefines =		mat.mShaderDefines;
	mInputElementDescs =	mat.mInputElementDescs;
	mInputLayout =			mat.mInputLayout;
	mTextures = mat.mTextures;
}

IMaterial* Material::Clone()
{
	Material* pMat = new Material(*this);
	pMat->SetAdam(const_cast<Material*>(this));

	mInstances.push_back(pMat);

	return pMat;
}

//----------------------------------------------------------------------------
Material::~Material()
{
	{
		WRITE_LOCK wl(mRWCSMaterial);
		mMaterials.erase(std::find(mMaterials.begin(), mMaterials.end(), this));
	}
	if (mAdamMaterial)
	{
		mAdamMaterial->RemoveInstance(this);
	}
}

//----------------------------------------------------------------------------
// IMaterial Interfaces
//----------------------------------------------------------------------------
bool Material::LoadFromFile(const char* filepath)
{
	mName = filepath;
	tinyxml2::XMLDocument doc;
	doc.LoadFile(filepath);
	if (doc.Error())
	{
		Log(FB_DEFAULT_DEBUG_ARG, "Error while parsing material!");
		if (doc.ErrorID()==tinyxml2::XML_ERROR_FILE_NOT_FOUND)
		{
			Log("Material %s is not found!", filepath);
		}
		const char* errMsg = doc.GetErrorStr1();
		if (errMsg)
			Log("\t%s", errMsg);
		errMsg = doc.GetErrorStr2();
		if (errMsg)
			Log("\t%s", errMsg);

		Log("\t loading missing material.");
		doc.LoadFile("es/materials/missing.material");
		if (doc.Error())
		{
			Log("Loading missing material is also failed!");
			return false;
		}
	}
	
	tinyxml2::XMLElement* pRoot = doc.FirstChildElement("Material");
	if (!pRoot)
	{
		assert(0);
		return false;
	}

	tinyxml2::XMLElement* pMaterialConstants = pRoot->FirstChildElement("MaterialConstants");
	if (pMaterialConstants)
	{
		tinyxml2::XMLElement* pAmbientElem = pMaterialConstants->FirstChildElement("AmbientColor");
		if (pAmbientElem)
		{
			const char* szAmbient = pAmbientElem->GetText();
			Vec4 ambient(szAmbient);			
			SetAmbientColor(ambient);
		}
		tinyxml2::XMLElement* pDiffuseElem = pMaterialConstants->FirstChildElement("DiffuseColor_Alpha");
		if (pDiffuseElem)
		{
			const char* szDiffuse = pDiffuseElem->GetText();
			Vec4 diffuse(szDiffuse);			
			SetDiffuseColor(diffuse);
		}
		tinyxml2::XMLElement* pSpecularElem = pMaterialConstants->FirstChildElement("SpecularColor_Shine");
		if (pSpecularElem)
		{
			const char* szSpecular = pSpecularElem->GetText();
			Vec4 specular(szSpecular);			
			SetSpecularColor(specular);
		}
		tinyxml2::XMLElement* pEmissiveElem = pMaterialConstants->FirstChildElement("EmissiveColor_Strength");
		if (pEmissiveElem)
		{
			const char* szEmissive = pEmissiveElem->GetText();
			Vec4 emissive(szEmissive);			
			SetEmissiveColor(emissive);
		}
	}

	tinyxml2::XMLElement* pMaterialParameters = pRoot->FirstChildElement("MaterialParameters");
	mMaterialParameters.clear();
	if (pMaterialParameters)
	{
		tinyxml2::XMLElement* pElem = pMaterialParameters->FirstChildElement();
		int i=0;
		while (pElem)
		{
			const char* szVector = pElem->GetText();
			if (szVector)
			{
				Vec4 v(szVector);
				mMaterialParameters.Insert(PARAMETER_VECTOR::value_type(i++, v));
			}
			pElem = pElem->NextSiblingElement();
		}
	}

	tinyxml2::XMLElement* pDefines= pRoot->FirstChildElement("ShaderDefines");
	mShaderDefines.clear();
	if (pDefines)
	{
		tinyxml2::XMLElement* pElem = pDefines->FirstChildElement();
		int i=0;
		while(pElem)
		{
			mShaderDefines.push_back(ShaderDefine());
			const char* pname = pElem->Attribute("name");
			if (pname)
				mShaderDefines.back().name = pname;
			const char* pval = pElem->Attribute("val");
			if (pval)
				mShaderDefines.back().value = pval;

			pElem = pElem->NextSiblingElement();
		}
	}
	
	tinyxml2::XMLElement* pTexturesElem = pRoot->FirstChildElement("Textures");
	if (pTexturesElem)
	{
		tinyxml2::XMLElement* pTexElem = pTexturesElem->FirstChildElement("Texture");
		while (pTexElem)
		{
			const char* filepath = pTexElem->GetText();
			int slot = 0;
			BINDING_SHADER shader = BINDING_SHADER_PS;
			SAMPLER_DESC samplerDesc;
			pTexElem->QueryIntAttribute("slot", &slot);
			const char* szShader = pTexElem->Attribute("shader");
			shader = BindingShaderFromString(szShader);
			const char* szAddressU = pTexElem->Attribute("AddressU");
			samplerDesc.AddressU = AddressModeFromString(szAddressU);
			const char* szAddressV = pTexElem->Attribute("AddressV");
			samplerDesc.AddressV = AddressModeFromString(szAddressV);
			const char* szFilter = pTexElem->Attribute("Filter");
			samplerDesc.Filter = FilterFromString(szFilter);
			ITexture* pTextureInTheSlot = 0;
			const char* szType = pTexElem->Attribute("type");
			TEXTURE_TYPE type = TextureTypeFromString(szType);
			ColorRamp cr;
			if (type == TEXTURE_TYPE_COLOR_RAMP)
			{
				tinyxml2::XMLElement* barElem = pTexElem->FirstChildElement("Bar");
				while (barElem)
				{
					float pos = barElem->FloatAttribute("pos");
					const char* szColor = barElem->GetText();
					Vec4 color(szColor);
					cr.InsertBar(pos, color);
					barElem = barElem->NextSiblingElement();
				}
				SetColorRampTexture(cr, shader, slot, samplerDesc);
			}
			else
			{
				SetTexture(filepath, shader, slot, samplerDesc);
			}
			pTexElem = pTexElem->NextSiblingElement("Texture");
		}
	}

	mShaders = BINDING_SHADER_VS | BINDING_SHADER_PS;
	tinyxml2::XMLElement* pShaders = pRoot->FirstChildElement("Shaders");
	if (pShaders)
	{
		const char* shaders = pShaders->GetText();
		if (shaders)
		{
			mShaders = 0;
			std::string strShaders = shaders;
			ToLowerCase(strShaders);
			if (strShaders.find("vs") != std::string::npos)
			{
				mShaders |= BINDING_SHADER_VS;
			}
			if (strShaders.find("hs") != std::string::npos)
			{
				mShaders |= BINDING_SHADER_HS;
			}
			if (strShaders.find("ds") != std::string::npos)
			{
				mShaders |= BINDING_SHADER_DS;
			}
			if (strShaders.find("gs") != std::string::npos)
			{
				mShaders |= BINDING_SHADER_GS;
			}
			if (strShaders.find("ps") != std::string::npos)
			{
				mShaders |= BINDING_SHADER_PS;
			}
		}
	}

	tinyxml2::XMLElement* pShaderFileElem = pRoot->FirstChildElement("ShaderFile");
	if (pShaderFileElem)
	{
		const char* shaderFile = pShaderFileElem->GetText();
		if (shaderFile)
		{
			mShader = gFBEnv->pEngine->GetRenderer()->CreateShader(shaderFile, mShaders, mShaderDefines);
			mShaderFile = mShader->GetName();
		}
	}

	tinyxml2::XMLElement* pInputLayoutElem = pRoot->FirstChildElement("InputLayout");
	mInputElementDescs.clear();
	if (pInputLayoutElem)
	{
		tinyxml2::XMLElement* pElem = pInputLayoutElem->FirstChildElement();
		int i=0;
		while(pElem)
		{
			mInputElementDescs.push_back(INPUT_ELEMENT_DESC());
			const char* pbuffer = pElem->Attribute("semantic");
			if (pbuffer)
				mInputElementDescs.back().mSemanticName = pbuffer;
			mInputElementDescs.back().mSemanticIndex = pElem->IntAttribute("index");

			pbuffer = pElem->Attribute("format");
			if (pbuffer)
			{
				mInputElementDescs.back().mFormat = ConvertInputElement(pbuffer);
			}

			mInputElementDescs.back().mInputSlot = pElem->IntAttribute("slot");

			mInputElementDescs.back().mAlignedByteOffset = pElem->IntAttribute("alignedByteOffset");

			pbuffer = pElem->Attribute("inputSlotClass");
			if (pbuffer)
				mInputElementDescs.back().mInputSlotClass = ConvertInputClassification(pbuffer);

			mInputElementDescs.back().mInstanceDataStepRate = pElem->IntAttribute("stepRate");

			pElem = pElem->NextSiblingElement();
		}
	}

	return true;
}

void Material::RemoveInstance(Material* pInstance)
{
	mInstances.erase(std::remove(mInstances.begin(), mInstances.end(), pInstance),
		mInstances.end());
}

//----------------------------------------------------------------------------
void Material::ReloadMaterial(const char* name)
{
	std::string filepath(name);
	ToLowerCase(filepath);
	READ_LOCK rl(mRWCSMaterial);
	for each(auto mat in mMaterials)
	{
		if (strcmp(mat->GetName(), filepath.c_str())==0)
		{
			// not reloading instances.
			mat->RegisterReloading();
			return;
		}
	}
}

//----------------------------------------------------------------------------
void Material::ReloadShader(const char* shader)
{
	std::string shaderPath(shader);
	ToLowerCase(shaderPath);
	std::vector<SHADER_DEFINES> reloaded;
	READ_LOCK rl(mRWCSMaterial);
	for each(auto mat in mMaterials)
	{
		if (strcmp(shaderPath.c_str(), mat->GetShaderFile())==0)
		{
			auto itFind = std::find(reloaded.begin(), reloaded.end(), mat->GetShaderDefines());
			if (itFind==reloaded.end())
			{
				Shader::ReloadShader(shader, mat->GetShaderDefines());
				reloaded.push_back(mat->GetShaderDefines());
			}
		}
	}
}

//----------------------------------------------------------------------------
bool Material::FindTextureIn(BINDING_SHADER shader, int slot, ITexture** pTextureInTheSlot,
	TextureSignature* pSignature/*=0*/) const
{
	*pTextureInTheSlot = 0;
	auto it = mTextures.begin(), itEnd = mTextures.end();
	for (; it!=itEnd; it++)
	{
		if ((*it)->GetSlot() == slot && (*it)->GetShaderStage() == shader)
		{
			*pTextureInTheSlot = (*it);
			break;
		}
	}
	if (!(*pTextureInTheSlot))
		return false;
		
	if (pSignature)
	{
		ITexture* pTexture = *pTextureInTheSlot;
		if (pSignature->mType != TEXTURE_TYPE_COUNT &&
			pSignature->mType != pTexture->GetType())
			return false;

		if (pSignature->mFilepath != pTexture->GetName())
			return false;

		if (pSignature->mColorRamp)
		{
			auto itFound = mColorRampMap.find(pTexture);
			if ( itFound==mColorRampMap.end() || 
				!(itFound->second == *pSignature->mColorRamp) )
			{
				return false; // not same
			}
		}
	}

	return true; // same
}

//----------------------------------------------------------------------------
void Material::SetAmbientColor(float r, float g, float b, float a)
{
	mMaterialConstants.gAmbientColor = Vec4(r, g, b, a);
}

//----------------------------------------------------------------------------
void Material::SetAmbientColor(const Vec4& ambient)
{
	mMaterialConstants.gAmbientColor = ambient;
}

//----------------------------------------------------------------------------
void Material::SetDiffuseColor(float r, float g, float b, float a)
{
	mMaterialConstants.gDiffuseColor = Vec4(r, g, b, a);
}

//----------------------------------------------------------------------------
void Material::SetDiffuseColor(const Vec4& diffuse)
{
	mMaterialConstants.gDiffuseColor = diffuse;
}

//----------------------------------------------------------------------------
void Material::SetSpecularColor(float r, float g, float b, float shine)
{
	mMaterialConstants.gSpecularColor = Vec4(r, g, b, shine);
}

//----------------------------------------------------------------------------
void Material::SetSpecularColor(const Vec4& specular)
{
	mMaterialConstants.gSpecularColor = specular;
}

//----------------------------------------------------------------------------
void Material::SetEmissiveColor(float r, float g, float b, float strength)
{
	mMaterialConstants.gEmissiveColor = Vec4(r, g, b, strength);
}

//----------------------------------------------------------------------------
void Material::SetEmissiveColor(const Vec4& emissive)
{
	mMaterialConstants.gEmissiveColor = emissive;
}

//----------------------------------------------------------------------------
void Material::SetTexture(const char* filepath, BINDING_SHADER shader, int slot, 
	const SAMPLER_DESC& samplerDesc)
{
	ITexture* pTexture = 0;
	TextureSignature signature(TEXTURE_TYPE_DEFAULT, filepath, 0);
	bool same = FindTextureIn(shader, slot, &pTexture, &signature);

	if (same)
	{
		assert(pTexture);
		pTexture->SetSamplerDesc(samplerDesc);
		return;
	}

	if (!pTexture)
	{
		pTexture = gFBEnv->pEngine->GetRenderer()->CreateTexture(filepath);
		pTexture->SetSlot(slot);
		pTexture->SetShaderStage(shader);
		pTexture->SetSamplerDesc(samplerDesc);
		pTexture->SetType(TEXTURE_TYPE_DEFAULT);
		mTextures.push_back(pTexture);
	}
}

void Material::SetTexture(ITexture* pTexture, BINDING_SHADER shader,  int slot,
			const SAMPLER_DESC& samplerDesc)
{	
	ITexture* pTextureInSlot = 0;
	bool same = FindTextureIn(shader, slot, &pTextureInSlot);
	if (pTextureInSlot!=pTexture)
	{
		RemoveTexture(pTextureInSlot);
	}

	if (pTexture)
	{
		pTexture->SetSlot(slot);
		pTexture->SetShaderStage(shader);
		pTexture->SetSamplerDesc(samplerDesc);
		pTexture->SetType(TEXTURE_TYPE_DEFAULT);
		mTextures.push_back(pTexture);
	}
}

void Material::SetColorRampTexture(ColorRamp& cr, BINDING_SHADER shader, int slot, 
			const SAMPLER_DESC& samplerDesc)
{
	ITexture* pTexture = 0;
	TextureSignature signature(TEXTURE_TYPE_COLOR_RAMP, 0, &cr);
	bool same = FindTextureIn(shader, slot, &pTexture, &signature);

	if (same)
	{
		assert(pTexture);
		mColorRampMap[pTexture] = cr;
		RefreshColorRampTexture(slot, shader);
	}
	
	if (!pTexture)
	{
		pTexture = CreateColorRampTexture(cr);
		pTexture->SetSlot(slot);
		pTexture->SetShaderStage(shader);
		pTexture->SetSamplerDesc(samplerDesc);
		pTexture->SetType(TEXTURE_TYPE_COLOR_RAMP);
		mTextures.push_back(pTexture);
	}
}

//----------------------------------------------------------------------------
ColorRamp& Material::GetColorRamp(int slot, BINDING_SHADER shader)
{
	ITexture* pTexture = 0;
	TextureSignature signature(TEXTURE_TYPE_COLOR_RAMP, 0, 0);
	FindTextureIn(shader, slot, &pTexture, &signature);
	assert(pTexture->GetType() == TEXTURE_TYPE_COLOR_RAMP);

	return mColorRampMap[pTexture];

}

//----------------------------------------------------------------------------
void Material::RefreshColorRampTexture(int slot, BINDING_SHADER shader)
{
	ITexture* pTexture = 0;
	FindTextureIn(shader, slot, &pTexture);
	assert(pTexture!=0 && pTexture->GetType() == TEXTURE_TYPE_COLOR_RAMP);

	SAMPLER_DESC desc = pTexture->GetSamplerDesc();
	ColorRamp cr = mColorRampMap[pTexture];
	
	MapData data = pTexture->Map(0, MAP_TYPE_WRITE_DISCARD, MAP_FLAG_NONE);
	if (data.pData)
	{
		// bar position is already updated. generate ramp texture data.
		cr.GenerateColorRampTextureData(128);
		
		unsigned int *pixels = (unsigned int*)data.pData;
		for(unsigned x = 0; x < 128; x++) 
		{
			pixels[127-x] = cr[x].Get4Byte();
		}
		pTexture->Unmap(0);
	}
	RemoveTexture(pTexture);

	mColorRampMap[pTexture] = cr;
}

//----------------------------------------------------------------------------
ITexture* Material::CreateColorRampTexture(ColorRamp& cr)
{
	cr.GenerateColorRampTextureData(128);
	FIBITMAP* bitmap = FreeImage_Allocate(128, 1, 32);
	if (!bitmap)
	{
		IEngine::Log(FB_DEFAULT_DEBUG_ARG, "Failed to create freeimage!");
		assert(0);
		return 0;
	}
	unsigned int *pixels = (unsigned int *)FreeImage_GetScanLine(bitmap, 0);
	for(unsigned x = 0; x < 128; x++) 
	{
		pixels[127-x] = cr[x].Get4Byte();
	}

	ITexture* pTexture = gFBEnv->pEngine->GetRenderer()->CreateTexture(pixels, 128, 1, PIXEL_FORMAT_R8G8B8A8_UNORM,
		BUFFER_USAGE_DYNAMIC, BUFFER_CPU_ACCESS_WRITE, TEXTURE_TYPE_DEFAULT); // default is right.
	FreeImage_Unload(bitmap);

	mColorRampMap.insert(COLOR_RAMP_MAP_TYPE::value_type(pTexture, cr));
	return pTexture;
}

//----------------------------------------------------------------------------
void Material::RemoveTexture(ITexture* pTexture)
{
	if (!pTexture)
		return;

	mColorRampMap.erase(pTexture);
	mTextures.erase(std::remove(mTextures.begin(), mTextures.end(), pTexture), mTextures.end());
}

void Material::RemoveTexture(BINDING_SHADER shader, int slot)
{
	ITexture* pTexture = 0;
	FindTextureIn(shader, slot, &pTexture);
	RemoveTexture(pTexture);
}

//----------------------------------------------------------------------------
void Material::SetShaderDefines(const char* name, const char* val)
{
	if (name==0 || val==0)
		return;
	for each(auto d in mShaderDefines)
	{
		if (d.name==name)
		{
			d.value = val;
			return;
		}
	}
	mShaderDefines.push_back(ShaderDefine());
	mShaderDefines.back().name = name;
	mShaderDefines.back().value = val;
}

//----------------------------------------------------------------------------
void Material::SetMaterialParameters(unsigned index, const Vec4& value)
{
	mMaterialParameters[index] = value;
}

//----------------------------------------------------------------------------
const Vec4& Material::GetAmbientColor() const
{
	return mMaterialConstants.gAmbientColor;
}

//----------------------------------------------------------------------------
const Vec4& Material::GetDiffuseColor() const
{
	return mMaterialConstants.gDiffuseColor;
}

//----------------------------------------------------------------------------
const Vec4& Material::GetSpecularColor() const
{
	return mMaterialConstants.gSpecularColor;
}

//----------------------------------------------------------------------------
const Vec4& Material::GetEmissiveColor() const
{
	return mMaterialConstants.gEmissiveColor;
}

//----------------------------------------------------------------------------
const char* Material::GetShaderFile() const
{
	if (mShader)
		return mShader->GetName();

	return 0;
}

//----------------------------------------------------------------------------
void* Material::GetShaderByteCode(unsigned& size) const
{
	if (!mShader)
	{
		size = 0;
		return 0;
	}
	void* p = mShader->GetVSByteCode(size);
	return p;
}

//----------------------------------------------------------------------------
const Vec4& Material::GetMaterialParameters(unsigned index) const
{
	auto it = mMaterialParameters.Find(index);
	return it->second;
}

//----------------------------------------------------------------------------
void Material::Bind(bool inputLayout)
{
	if (mReloading && mReloadingTryCount < 10)
	{
		if (LoadFromFile(mName.c_str()))
		{
			mReloading = false;
			mReloadingTryCount = 0;
		}
		else
		{
			mReloadingTryCount++;
		}
	}
	if (!mShader)
	{
		assert(0);
		mShader = gFBEnv->pEngine->GetRenderer()->CreateShader(
			mShaderFile.c_str(), mShaders, mShaderDefines);
	}

	if (mShader)
	{
		mShader->Bind();
	}

	if (!mInputLayout && inputLayout)
	{
		mInputLayout = gFBEnv->pRenderer->GetInputLayout(mInputElementDescs, this);
	}
	if (mInputLayout && inputLayout)
	{
		mInputLayout->Bind();
	}
	gFBEnv->pEngine->GetRenderer()->UpdateMaterialConstantsBuffer(&mMaterialConstants);

	if (!mMaterialParameters.empty())
	{
		Vec4* pDest = (Vec4*)gFBEnv->pEngine->GetRenderer()->MapMaterialParameterBuffer();
		auto it = mMaterialParameters.begin(), itEnd = mMaterialParameters.end();
		for (; it!=itEnd; it++)
		{
			Vec4* pCurDest = pDest + it->first;
			memcpy(pCurDest, &(it->second), sizeof(Vec4));
		}
		gFBEnv->pEngine->GetRenderer()->UnmapMaterialParameterBuffer();
	}

	auto it = mTextures.begin(),
		itEnd = mTextures.end();
	for( ; it!=itEnd; it++)
	{
		(*it)->Bind();
	}
}

//----------------------------------------------------------------------------
void Material::RegisterReloading()
{
	mReloading = true;
	mReloadingTryCount = 0;
	mInputLayout = 0;
}