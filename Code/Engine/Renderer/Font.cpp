#include <Engine/StdAfx.h>
#include <Engine/Renderer/Font.h>
#include <Engine/ITexture.h>
#include <Engine/GlobalEnv.h>
#include <Engine/IEngine.h>
#include <Engine/IRenderer.h>
#include <Engine/ICamera.h>
#include <Engine/Renderer/RendererEnums.h>
#include <Engine/IVertexBuffer.h>
#include <Engine/IShader.h>
#include <Engine/IInputLayout.h>
#include <CommonLib/Math/fbMath.h>
#include <CommonLib/Math/Vec2.h>
#include <CommonLib/Profiler.h>
#include <CommonLib/Unicode.h>

namespace fastbird
{
	//----------------------------------------------------------------------------
	// FontLoader
	//----------------------------------------------------------------------------
	class FontLoader
	{
	public:
		FontLoader(FILE *f, Font *font, const char *fontFile);

		virtual int Load() = 0; // Must be implemented by derived class

	protected:
		void LoadPage(int id, const char *pageFile, const std::string& fontFile);
		void SetFontInfo(int outlineThickness);
		void SetCommonInfo(int fontHeight, int base, int scaleW, int scaleH, int pages, bool isPacked);
		void AddChar(int id, int x, int y, int w, int h, int xoffset, int yoffset, int xadvance, int page, int chnl);
		void AddKerningPair(int first, int second, int amount);

		FILE *f;
		Font *font;
		std::string fontFile;

		int outlineThickness;
	};

	class FontLoaderTextFormat : public FontLoader
	{
	public:
		FontLoaderTextFormat(FILE *f, Font *font, const char *fontFile);

		int Load();

		int SkipWhiteSpace(const std::string &str, int start);
		int FindEndOfToken(const std::string &str, int start);

		void InterpretInfo(const std::string &str, int start);
		void InterpretCommon(const std::string &str, int start);
		void InterpretChar(const std::string &str, int start);
		void InterpretSpacing(const std::string &str, int start);
		void InterpretKerning(const std::string &str, int start);
		// intentionally not reference
		void InterpretPage(const std::string str, int start, const std::string fontFile);
	};

	class FontLoaderBinaryFormat : public FontLoader
	{
	public:
		FontLoaderBinaryFormat(FILE *f, Font *font, const char *fontFile);

		int Load();

		void ReadInfoBlock(int size);
		void ReadCommonBlock(int size);
		void ReadPagesBlock(int size);
		void ReadCharsBlock(int size);
		void ReadKerningPairsBlock(int size);
	};

	const unsigned int Font::MAX_BATCH = 4*2000;
	//----------------------------------------------------------------------------
}

using namespace fastbird;

//----------------------------------------------------------------------------
Font::Font()
{
	mFontHeight = 0;
	mBase = 0;
	mScaleW = 0;
	mScaleH = 0;
	mScale = 1.0f;
	mVertexLocation = 0;
	mHasOutline = false;
	mEncoding = NONE;
	mColor = 0xFFFFFFFF;
	mInitialized = false;
}

//----------------------------------------------------------------------------
Font::~Font()
{
	std::map<int, SCharDescr*>::iterator it = mChars.begin();
	while( it != mChars.end() )
	{
		delete it->second;
		it++;
	}
}

//----------------------------------------------------------------------------
// IFont
//----------------------------------------------------------------------------
int Font::Init(const char *fontFile)
{
	Profiler profiler("'Font Init'");
	// Load the font
	FILE *f = 0;
#if _MSC_VER >= 1400 // MSVC 8.0 / 2005
	fopen_s(&f, fontFile, "rb");
#else
	f = fopen(fontFile, "rb");
#endif
	if( f == 0 )
	{
		IEngine::Log("Failed to open font file '%s'!", fontFile);
		return -1;
	}

	// Determine format by reading the first bytes of the file
	char str[4] = {0};
	fread(str, 3, 1, f);
	fseek(f, 0, SEEK_SET);
	int r = 0;
	{
		Profiler profiler("'Font loding'");
		FontLoader *loader = 0;
		if( strcmp(str, "BMF") == 0 )
			loader = new FontLoaderBinaryFormat(f, this, fontFile);
		else
			loader = new FontLoaderTextFormat(f, this, fontFile);

		r = loader->Load();
		delete loader;
	}

	mVertexBuffer = gFBEnv->pEngine->GetRenderer()->CreateVertexBuffer(0, sizeof(FontVertex), MAX_BATCH, 
		BUFFER_USAGE_DYNAMIC, BUFFER_CPU_ACCESS_WRITE); 
	assert(mVertexBuffer);

	// init shader
	mShader = gFBEnv->pEngine->GetRenderer()->CreateShader(
		"Code/Engine/Renderer/Shaders/font.hlsl", BINDING_SHADER_VS | BINDING_SHADER_PS,
		IMaterial::SHADER_DEFINES());
	mInputLayout = gFBEnv->pEngine->GetRenderer()->GetInputLayout(
		DEFAULT_INPUTS::POSITION_COLOR_TEXCOORD_BLENDINDICES, mShader);
		
	mInitialized = r==0;

	RASTERIZER_DESC rd;
	SetRasterizerState(rd);
	rd.ScissorEnable = true;
	mScissorEnabledState = gFBEnv->pRenderer->CreateRasterizerState(rd);
	DEPTH_STENCIL_DESC ds;
	mDepthEnabledState = gFBEnv->pRenderer->CreateDepthStencilState(ds); 
	ds.DepthEnable = false;
	SetDepthStencilState(ds);

	SetBlendState(BLEND_DESC());
	mObjectConstants.gWorldViewProj = MakeOrthogonalMatrix(0, 0, 
		(float)gFBEnv->pEngine->GetRenderer()->GetWidth(),
		(float)gFBEnv->pEngine->GetRenderer()->GetHeight(),
		0.f, 1.0f);
	mObjectConstants.gWorld.MakeIdentity();

	
	return r;
}

//----------------------------------------------------------------------------
void Font::SetTextEncoding(EFontTextEncoding encoding)
{
	mEncoding = encoding;
}

//----------------------------------------------------------------------------
void Font::InternalWrite(float x, float y, float z, const char *text, int count, float spacing)
{
	static FontVertex vertices[MAX_BATCH];

	int page = -1;
	y -= mScale * float(mBase);
	unsigned int batchingVertices = 0;
	for( int n = 0; n < count; )
	{
		int charId = GetTextChar(text, n, &n);
		SCharDescr *ch = GetChar(charId);
		if( ch == 0 ) 
			ch = &mDefChar;

		// Map the center of the texel to the corners
		// in order to get pixel perfect mapping
		float u = (float(ch->srcX)) / (float)mScaleW;
		float v = (float(ch->srcY)) / (float)mScaleH;
		float u2 = u + float(ch->srcW) / (float)mScaleW;
		float v2 = v + float(ch->srcH) / (float)mScaleH;

		float a = mScale * float(ch->xAdv);
		float w = mScale * float(ch->srcW);
		float h = mScale * float(ch->srcH);
		float ox = mScale * float(ch->xOff);
		float oy = mScale * float(ch->yOff);

		if( ch->page != page)
		{
			if (batchingVertices)
				Flush(page, vertices, batchingVertices);

			mVertexLocation+=batchingVertices;
			batchingVertices = 0;
			page = ch->page;
			mPages[page]->Bind();
		}

		if ( mVertexLocation + batchingVertices + 4 >= MAX_BATCH )
		{
			Flush(page, vertices, batchingVertices);
			batchingVertices = 0;
			mVertexLocation = 0;
		}

		float left = x+ox;
		float top = y+oy;
		float right = left + w;
		float bottom = top + h;

		vertices[mVertexLocation + batchingVertices++] = FontVertex(
			Vec3(left, top, z), mColor, Vec2(u, v), ch->chnl);
		vertices[mVertexLocation + batchingVertices++] = FontVertex(
			Vec3(right, top, z), mColor, Vec2(u2, v), ch->chnl);
		vertices[mVertexLocation + batchingVertices++] = FontVertex(
			Vec3(left, bottom, z), mColor, Vec2(u, v2), ch->chnl);
		vertices[mVertexLocation + batchingVertices++] = FontVertex(
			Vec3(right, bottom, z), mColor, Vec2(u2, v2), ch->chnl);		

		x += a;
		if( charId == ' ' )
			x += spacing;

		if( n < count )
			x += AdjustForKerningPairs(charId, GetTextChar(text,n));
	}
	Flush(page, vertices, batchingVertices);
	mVertexLocation+=batchingVertices;
}

//----------------------------------------------------------------------------
void Font::Flush(int page, const FontVertex* pVertices, unsigned int vertexCount)
{
	if (page==-1 || vertexCount ==0)
		return;

	IRenderer* pRenderer = gFBEnv->pEngine->GetRenderer();

	MapData data = pRenderer->MapVertexBuffer(mVertexBuffer, 0, 
		MAP_TYPE_WRITE_NO_OVERWRITE, MAP_FLAG_NONE);
	FontVertex* pDest = (FontVertex*)data.pData;
	memcpy(pDest + mVertexLocation, pVertices + mVertexLocation, 
		vertexCount * sizeof(FontVertex));

	pRenderer->UnmapVertexBuffer(mVertexBuffer, 0);	
	mVertexBuffer->Bind();
	pRenderer->SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	pRenderer->Draw(vertexCount, mVertexLocation);
}

//----------------------------------------------------------------------------
void Font::Write(float x, float y, float z, unsigned int color, 
	const char *text, int count, FONT_ALIGN mode)
{
	if (!mInitialized)
		return;
	if( count < 0 )
		count = GetTextLength(text);

	if( mode == FONT_ALIGN_CENTER )
	{
		float w = GetTextWidth(text, count);
		x -= w/2;
	}
	else if( mode == FONT_ALIGN_RIGHT )
	{
		float w = GetTextWidth(text, count);
		x -= w;
	}

	mColor = color;

	InternalWrite(x, y, z, text, count);
}

//----------------------------------------------------------------------------
void Font::SetHeight(float h)
{
	mScale = h / float(mBase);
}

void Font::SetBackToOrigHeight()
{
	mScale = 1.f;
}

//----------------------------------------------------------------------------
float Font::GetHeight() const
{
	return mScale * mBase;
}

//----------------------------------------------------------------------------
float Font::GetTextWidth(const char *text, int count, float *outMinY/* = 0*/, float *outMaxY/* = 0*/)
{
	if( count < 0 )
		count = GetTextLength(text);

	float x = 0;
	float minY = 10000;
	float maxY = -10000;
	
	for( int n = 0; n < count; )
	{
		int charId = GetTextChar(text,n,&n);

		SCharDescr *ch = GetChar(charId);
		if( ch == 0 ) ch = &mDefChar;

		x += mScale * (ch->xAdv);
		float h = mScale * float(ch->srcH);
		float y = mScale * (float(mBase) - float(ch->yOff));
		if( minY > y-h )
			minY = y-h;
		if( maxY < y )
			maxY = y;

		if( n < count )
			x += AdjustForKerningPairs(charId, GetTextChar(text,n));
	}

	if( outMinY ) *outMinY = minY;
	if( outMaxY ) *outMaxY = maxY;

	return x;
}

//----------------------------------------------------------------------------
void Font::PrepareRenderResources()
{
	if (!mInitialized)
		return;
	mShader->Bind();
	mInputLayout->Bind();
}

void Font::SetRenderStates(bool depthEnable, bool scissorEnable)
{
	if (!mInitialized)
		return;

	if (scissorEnable)
	{
		mScissorEnabledState->Bind();
	}
	else
	{
		mRasterizerState->Bind();
	}
	mBlendState->Bind();	
	if (depthEnable)
	{
		mDepthEnabledState->Bind(0);
	}
	else
	{
		mDepthStencilState->Bind(0);
	}
}

void Font::SetDefaultConstants()
{
	if (!mInitialized)
		return;
	gFBEnv->pEngine->GetRenderer()->UpdateObjectConstantsBuffer(&mObjectConstants);
}

//----------------------------------------------------------------------------
int Font::GetTextLength(const char *text)
{
	if( mEncoding == UTF16 )
	{
		int textLen = 0;
		for(;;)
		{
			unsigned int len;
			int r = DecodeUTF16((const unsigned char *)&text[textLen], &len);
			if( r > 0 )
				textLen += len;
			else if( r < 0 )
				textLen++;
			else
				return textLen;
		}
	}

	// Both UTF8 and standard ASCII strings can use strlen
	return (int)strlen(text);
}

//----------------------------------------------------------------------------
// Own
//----------------------------------------------------------------------------
float Font::GetBottomOffset()
{
	return mScale * (mBase - mFontHeight);
}

float Font::GetTopOffset()
{
	return mScale * (mBase - 0);
}

//----------------------------------------------------------------------------
float Font::AdjustForKerningPairs(int first, int second)
{	
	SCharDescr *ch = GetChar(first);
	if( ch == 0 ) return 0;
	for( UINT n = 0; n < ch->kerningPairs.size(); n += 2 )
	{
		if( ch->kerningPairs[n] == second )
			return ch->kerningPairs[n+1] * mScale;
	}

	return 0;
}

//----------------------------------------------------------------------------
SCharDescr *Font::GetChar(int id)
{
	std::map<int, SCharDescr*>::iterator it = mChars.find(id);
	if( it == mChars.end() ) return 0;

	return it->second;
}

//----------------------------------------------------------------------------
int Font::GetTextChar(const char *text, int pos, int *nextPos)
{
	int ch;
	unsigned int len;
	if( mEncoding == UTF8 )
	{
		ch = DecodeUTF8((const unsigned char *)&text[pos], &len);
		if( ch == -1 ) len = 1;
	}
	else if( mEncoding == UTF16 )
	{
		ch = DecodeUTF16((const unsigned char *)&text[pos], &len);
		if( ch == -1 ) len = 2;
	}
	else
	{
		len = 1;
		ch = (unsigned char)text[pos];
	}

	if( nextPos ) *nextPos = pos + len;
	return ch;
}

//=============================================================================
// FontLoader
//
// This is the base class for all loader classes. This is the only class
// that has access to and knows how to set the Font members.
//=============================================================================

FontLoader::FontLoader(FILE *f, Font *font, const char *fontFile)
{
	this->f = f;
	this->font = font;
	this->fontFile = fontFile;

	outlineThickness = 0;
}

void FontLoader::LoadPage(int id, const char *pageFile, const std::string& fontFile)
{
	std::string str;

	// Load the texture from the same directory as the font descriptor file

	// Find the directory
	str = fontFile;
	for( size_t n = 0; (n = str.find('/', n)) != std::string::npos; ) str.replace(n, 1, "\\");
	size_t i = str.rfind('\\');
	if( i != std::string::npos )
		str = str.substr(0, i+1);
	else
		str = "";

	// Load the font textures
	str += pageFile;
	font->mPages[id] = gFBEnv->pEngine->GetRenderer()->CreateTexture(str.c_str());
	font->mPages[id]->SetShaderStage(BINDING_SHADER_PS);
	font->mPages[id]->SetSlot(0);

	//font->mPages[id]->SetSamplerDesc(SAMPLER_DESC());
	
	if( font->mPages[id]==0 )
		IEngine::Log("Failed to load font page '%s'!", str.c_str());
}

void FontLoader::SetFontInfo(int outlineThickness)
{
	this->outlineThickness = outlineThickness;
}

void FontLoader::SetCommonInfo(int fontHeight, int base, int scaleW, int scaleH, int pages, bool isPacked)
{
	font->mFontHeight = fontHeight;
	font->mBase = base;
	font->mScaleW = scaleW;
	font->mScaleH = scaleH;
	font->mPages.resize(pages);
	for( int n = 0; n < pages; n++ )
		font->mPages[n] = 0;

	if( isPacked && outlineThickness )
		font->mHasOutline = true;
}

void FontLoader::AddChar(int id, int x, int y, int w, int h, int xoffset, int yoffset, int xadvance, int page, int chnl)
{
	// Convert to a 4 element vector
	// TODO: Does this depend on hardware? It probably does
	if     ( chnl == 1 ) chnl = 0x00010000;  // Blue channel
	else if( chnl == 2 ) chnl = 0x00000100;  // Green channel
	else if( chnl == 4 ) chnl = 0x00000001;  // Red channel
	else if( chnl == 8 ) chnl = 0x01000000;  // Alpha channel
	else chnl = 0;

	if( id >= 0 )
	{
		SCharDescr *ch = new SCharDescr;
		ch->srcX = x;
		ch->srcY = y;
		ch->srcW = w;
		ch->srcH = h;
		ch->xOff = xoffset;
		ch->yOff = yoffset;
		ch->xAdv = xadvance;
		ch->page = page;
		ch->chnl = chnl;

		font->mChars.insert(std::map<int, SCharDescr*>::value_type(id, ch));
	}

	if( id == -1 )
	{
		font->mDefChar.srcX = x;
		font->mDefChar.srcY = y;
		font->mDefChar.srcW = w;
		font->mDefChar.srcH = h;
		font->mDefChar.xOff = xoffset;
		font->mDefChar.yOff = yoffset;
		font->mDefChar.xAdv = xadvance;
		font->mDefChar.page = page;
		font->mDefChar.chnl = chnl;
	}
}

void FontLoader::AddKerningPair(int first, int second, int amount)
{
	if( first >= 0 && first < 256 && font->mChars[first] )
	{
		font->mChars[first]->kerningPairs.push_back(second);
		font->mChars[first]->kerningPairs.push_back(amount);
	}
}

//=============================================================================
// FontLoaderTextFormat
//
// This class implements the logic for loading a BMFont file in text format
//=============================================================================

FontLoaderTextFormat::FontLoaderTextFormat(FILE *f, Font *font, const char *fontFile) 
	: FontLoader(f, font, fontFile)
{
}

int FontLoaderTextFormat::Load()
{
	std::string line;

	while( !feof(f) )
	{
		// Read until line feed (or EOF)
		line = "";
		line.reserve(256);
		while( !feof(f) )
		{
			char ch;
			if( fread(&ch, 1, 1, f) )
			{
				if( ch != '\n' ) 
					line += ch; 
				else
					break;
			}
		}

		// Skip white spaces
		int pos = SkipWhiteSpace(line, 0);
		if( pos == line.size() ) 
			break;
		// Read token
		int pos2 = FindEndOfToken(line, pos);
		std::string token = line.substr(pos, pos2-pos);

		// Interpret line
		if( token == "info" )
		{
			InterpretInfo(line, pos2);
		}
		else if( token == "common" )
		{
			InterpretCommon(line, pos2);
		}
		else if( token == "char" )
		{
			InterpretChar(line, pos2);
		}
		else if( token == "kerning" )
		{
			InterpretKerning(line, pos2);
		}
		else if( token == "page" )
		{
			InterpretPage(line, pos2, fontFile);
		}
	}

	fclose(f);

	// Success
	return 0;
}

int FontLoaderTextFormat::SkipWhiteSpace(const std::string &str, int start)
{
	UINT n = start;
	while( n < str.size() )
	{
		char ch = str[n];
		if( ch != ' ' && 
			ch != '\t' && 
			ch != '\r' && 
			ch != '\n' )
			break;

		++n;
	}

	return n;
}

int FontLoaderTextFormat::FindEndOfToken(const std::string &str, int start)
{
	UINT n = start;
	if( str[n] == '"' )
	{
		n++;
		while( n < str.size() )
		{
			char ch = str[n];
			if( ch == '"' )
			{
				// Include the last quote char in the token
				++n;
				break;
			}
			++n;
		}
	}
	else
	{
		while( n < str.size() )
		{
			char ch = str[n];
			if( ch == ' ' ||
				ch == '\t' ||
				ch == '\r' ||
				ch == '\n' ||
				ch == '=' )
				break;

			++n;
		}
	}

	return n;
}

void FontLoaderTextFormat::InterpretKerning(const std::string &str, int start)
{
	// Read the attributes
	int first = 0;
	int second = 0;
	int amount = 0;

	int pos, pos2 = start;
	while( true )
	{
		pos = SkipWhiteSpace(str, pos2);
		if( pos == str.size() ) 
			break;
		pos2 = FindEndOfToken(str, pos);

		std::string token = str.substr(pos, pos2-pos);

		pos = SkipWhiteSpace(str, pos2);
		if( pos == str.size() || str[pos] != '=' ) break;

		pos = SkipWhiteSpace(str, pos+1);
		pos2 = FindEndOfToken(str, pos);

		std::string value = str.substr(pos, pos2-pos);

		if( token == "first" )
			first = strtol(value.c_str(), 0, 10);
		else if( token == "second" )
			second = strtol(value.c_str(), 0, 10);
		else if( token == "amount" )
			amount = strtol(value.c_str(), 0, 10);

		if( pos == str.size() ) break;
	}

	// Store the attributes
	AddKerningPair(first, second, amount);
}

void FontLoaderTextFormat::InterpretChar(const std::string &str, int start)
{
	// Read all attributes
	int id = 0;
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
	int xoffset = 0;
	int yoffset = 0;
	int xadvance = 0;
	int page = 0;
	int chnl = 0;

	int pos, pos2 = start;
	while( true )
	{
		pos = SkipWhiteSpace(str, pos2);
		if( pos == str.size() ) 
			break;
		pos2 = FindEndOfToken(str, pos);

		std::string token = str.substr(pos, pos2-pos);

		pos = SkipWhiteSpace(str, pos2);
		if( pos == str.size() || str[pos] != '=' ) break;

		pos = SkipWhiteSpace(str, pos+1);
		pos2 = FindEndOfToken(str, pos);

		std::string value = str.substr(pos, pos2-pos);

		if( token == "id" )
			id = strtol(value.c_str(), 0, 10);
		else if( token == "x" )
			x = strtol(value.c_str(), 0, 10);
		else if( token == "y" )
			y = strtol(value.c_str(), 0, 10);
		else if( token == "width" )
			width = strtol(value.c_str(), 0, 10);
		else if( token == "height" )
			height = strtol(value.c_str(), 0, 10);
		else if( token == "xoffset" )
			xoffset = strtol(value.c_str(), 0, 10);
		else if( token == "yoffset" )
			yoffset = strtol(value.c_str(), 0, 10);
		else if( token == "xadvance" )
			xadvance = strtol(value.c_str(), 0, 10);
		else if( token == "page" )
			page = strtol(value.c_str(), 0, 10);
		else if( token == "chnl" )
			chnl = strtol(value.c_str(), 0, 10);

		if( pos == str.size() ) break;
	}

	// Store the attributes
	AddChar(id, x, y, width, height, xoffset, yoffset, xadvance, page, chnl);
}

void FontLoaderTextFormat::InterpretCommon(const std::string &str, int start)
{
	int fontHeight;
	int base;
	int scaleW;
	int scaleH;
	int pages;
	int packed;

	// Read all attributes
	int pos, pos2 = start;
	while( true )
	{
		pos = SkipWhiteSpace(str, pos2);
		if( pos == str.size() ) 
			break;
		pos2 = FindEndOfToken(str, pos);

		std::string token = str.substr(pos, pos2-pos);

		pos = SkipWhiteSpace(str, pos2);
		if( pos == str.size() || str[pos] != '=' ) break;

		pos = SkipWhiteSpace(str, pos+1);
		pos2 = FindEndOfToken(str, pos);

		std::string value = str.substr(pos, pos2-pos);

		if( token == "lineHeight" )
			fontHeight = (short)strtol(value.c_str(), 0, 10);
		else if( token == "base" )
			base = (short)strtol(value.c_str(), 0, 10);
		else if( token == "scaleW" )
			scaleW = (short)strtol(value.c_str(), 0, 10);
		else if( token == "scaleH" )
			scaleH = (short)strtol(value.c_str(), 0, 10);
		else if( token == "pages" )
			pages = strtol(value.c_str(), 0, 10);
		else if( token == "packed" )
			packed = strtol(value.c_str(), 0, 10);

		if( pos == str.size() ) break;
	}

	SetCommonInfo(fontHeight, base, scaleW, scaleH, pages, packed ? true : false);
}

void FontLoaderTextFormat::InterpretInfo(const std::string &str, int start)
{
	int outlineThickness;

	// Read all attributes
	int pos, pos2 = start;
	while( true )
	{
		pos = SkipWhiteSpace(str, pos2);
		if( pos == str.size() ) 
			break;
		pos2 = FindEndOfToken(str, pos);

		std::string token = str.substr(pos, pos2-pos);

		pos = SkipWhiteSpace(str, pos2);
		if( pos == str.size() || str[pos] != '=' ) 
			break;

		pos = SkipWhiteSpace(str, pos+1);
		pos2 = FindEndOfToken(str, pos);

		std::string value = str.substr(pos, pos2-pos);

		if( token == "outline" )
			outlineThickness = (short)strtol(value.c_str(), 0, 10);

		if( pos == str.size() ) 
			break;
	}

	SetFontInfo(outlineThickness);
}

void FontLoaderTextFormat::InterpretPage(const std::string str, int start, 
	const std::string fontFile)
{
	int id = 0;
	std::string file;

	// Read all attributes
	int pos, pos2 = start;
	while( true )
	{
		pos = SkipWhiteSpace(str, pos2);
		if( pos == str.size() ) 
			break;
		pos2 = FindEndOfToken(str, pos);

		std::string token = str.substr(pos, pos2-pos);

		pos = SkipWhiteSpace(str, pos2);
		if( pos == str.size() || str[pos] != '=' ) break;

		pos = SkipWhiteSpace(str, pos+1);
		pos2 = FindEndOfToken(str, pos);

		std::string value = str.substr(pos, pos2-pos);

		if( token == "id" )
			id = strtol(value.c_str(), 0, 10);
		else if( token == "file" )
			file = value.substr(1, value.length()-2);

		if( pos == str.size() ) break;
	}

	LoadPage(id, file.c_str(), fontFile);
}

//=============================================================================
// FontLoaderBinaryFormat
//
// This class implements the logic for loading a BMFont file in binary format
//=============================================================================

FontLoaderBinaryFormat::FontLoaderBinaryFormat(FILE *f, Font *font, const char *fontFile) 
	: FontLoader(f, font, fontFile)
{
}

int FontLoaderBinaryFormat::Load()
{
	// Read and validate the tag. It should be 66, 77, 70, 2, 
	// or 'BMF' and 2 where the number is the file version.
	char magicString[4];
	fread(magicString, 4, 1, f);
	if( strncmp(magicString, "BMF\003", 4) != 0 )
	{
		Log("Unrecognized format for '%s'", fontFile.c_str());
		fclose(f);
		return -1;
	}

	// Read each block
	char blockType;
	int blockSize;
	while( fread(&blockType, 1, 1, f) )
	{
		// Read the blockSize
		fread(&blockSize, 4, 1, f);

		switch( blockType )
		{
		case 1: // info
			ReadInfoBlock(blockSize);
			break;
		case 2: // common
			ReadCommonBlock(blockSize);
			break;
		case 3: // pages
			ReadPagesBlock(blockSize);
			break;
		case 4: // chars
			ReadCharsBlock(blockSize);
			break;
		case 5: // kerning pairs
			ReadKerningPairsBlock(blockSize);
			break;
		default:
			IEngine::Log("Unexpected block type (%d)", blockType);
			fclose(f);
			return -1;
		}
	}

	fclose(f);

	// Success
	return 0;
}

void FontLoaderBinaryFormat::ReadInfoBlock(int size)
{
#pragma pack(push)
#pragma pack(1)
struct infoBlock
{
    unsigned short fontSize;
    unsigned char  reserved:4;
    unsigned char  bold    :1;
    unsigned char  italic  :1;
    unsigned char  unicode :1;
    unsigned char  smooth  :1;
    unsigned char  charSet;
    unsigned short stretchH;
    unsigned char  aa;
    unsigned char  paddingUp;
    unsigned char  paddingRight;
    unsigned char  paddingDown;
    unsigned char  paddingLeft;
    unsigned char  spacingHoriz;
    unsigned char  spacingVert;
    unsigned char  outline;         // Added with version 2
    char fontName[1];
};
#pragma pack(pop)

	char *buffer = new char[size];
	fread(buffer, size, 1, f);

	// We're only interested in the outline thickness
	infoBlock *blk = (infoBlock*)buffer;
	SetFontInfo(blk->outline);

	delete[] buffer;
}

void FontLoaderBinaryFormat::ReadCommonBlock(int size)
{
#pragma pack(push)
#pragma pack(1)
struct commonBlock
{
    unsigned short lineHeight;
    unsigned short base;
    unsigned short scaleW;
    unsigned short scaleH;
    unsigned short pages;
    unsigned char  packed  :1;
    unsigned char  reserved:7;
	unsigned char  alphaChnl;
	unsigned char  redChnl;
	unsigned char  greenChnl;
	unsigned char  blueChnl;
}; 
#pragma pack(pop)

	char *buffer = new char[size];
	fread(buffer, size, 1, f);

	commonBlock *blk = (commonBlock*)buffer;

	SetCommonInfo(blk->lineHeight, blk->base, blk->scaleW, blk->scaleH, blk->pages, blk->packed ? true : false);

	delete[] buffer;
}

void FontLoaderBinaryFormat::ReadPagesBlock(int size)
{
#pragma pack(push)
#pragma pack(1)
struct pagesBlock
{
    char pageNames[1];
};
#pragma pack(pop)

	char *buffer = new char[size];
	fread(buffer, size, 1, f);

	pagesBlock *blk = (pagesBlock*)buffer;

	for( int id = 0, pos = 0; pos < size; id++ )
	{
		LoadPage(id, &blk->pageNames[pos], fontFile);
		pos += 1 + (int)strlen(&blk->pageNames[pos]);
	}

	delete[] buffer;
}

void FontLoaderBinaryFormat::ReadCharsBlock(int size)
{
#pragma pack(push)
#pragma pack(1)
struct charsBlock
{
    struct charInfo
    {
        DWORD id;
        unsigned short x;
        unsigned short y;
        unsigned short width;
        unsigned short height;
        short xoffset;
        short yoffset;
        short xadvance;
        unsigned char page;
        unsigned char chnl;
    } chars[1];
};
#pragma pack(pop)

	char *buffer = new char[size];
	fread(buffer, size, 1, f);

	charsBlock *blk = (charsBlock*)buffer;

	for( int n = 0; int(n*sizeof(charsBlock::charInfo)) < size; n++ )
	{
		AddChar(blk->chars[n].id,
		        blk->chars[n].x,
				blk->chars[n].y,
				blk->chars[n].width,
				blk->chars[n].height,
				blk->chars[n].xoffset,
				blk->chars[n].yoffset,
				blk->chars[n].xadvance,
				blk->chars[n].page,
				blk->chars[n].chnl);
	}

	delete[] buffer;
}

void FontLoaderBinaryFormat::ReadKerningPairsBlock(int size)
{
#pragma pack(push)
#pragma pack(1)
struct kerningPairsBlock
{
    struct kerningPair
    {
        DWORD first;
        DWORD second;
        short amount;
    } kerningPairs[1];
};
#pragma pack(pop)

	char *buffer = new char[size];
	fread(buffer, size, 1, f);

	kerningPairsBlock *blk = (kerningPairsBlock*)buffer;

	for( int n = 0; int(n*sizeof(kerningPairsBlock::kerningPair)) < size; n++ )
	{
		AddKerningPair(blk->kerningPairs[n].first,
		               blk->kerningPairs[n].second,
					   blk->kerningPairs[n].amount);
	}

	delete[] buffer;
}