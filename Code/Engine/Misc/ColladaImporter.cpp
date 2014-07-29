#include <Engine/StdAfx.h>
#include <Engine/Misc/ColladaImporter.h>
#include <Engine/IMeshObject.h>
#include <Engine/RenderObjects/MeshGroup.h>
#include <Engine/Renderer/RendererStructs.h>
#include <CommonLib/MurmurHash.h>
#include <CommonLib/Profiler.h>
#include <COLLADASaxFWLLoader.h>
#include <COLLADAFW.h>

namespace fastbird
{

IColladaImporter* IColladaImporter::CreateColladaImporter()
{
	return new ColladaImporter;
}

ColladaImporter::ColladaImporter()
	: mSwapYZ(false)
	, mOppositeCull(true)
	, mUseIndexBuffer(false)
	, mGenerateTangent(false)
	, mKeepMeshdata(false)
	, mNumMeshes(0)
{

}

ColladaImporter::~ColladaImporter()
{
	mMeshObjects.clear();
}

bool ColladaImporter::ImportCollada(const char* filepath, bool yzSwap, bool oppositeCull, bool useIndexBuffer, 
	bool mergeMatGroups, bool keepMeshData, bool generateTangent, bool meshGroup)
{
	mMeshObjects.clear();
	mGenerateTangent = generateTangent;
	Profiler profiler("'Import Collada'");
	bool successful = false;
	mKeepMeshdata = keepMeshData;
	mUseMeshGroup = meshGroup;
	mMergeMaterialGroups = mergeMatGroups;
	mSwapYZ = yzSwap;
	mOppositeCull = oppositeCull;
	mUseIndexBuffer = useIndexBuffer;
	if (strcmp(filepath, mFilepath.c_str()))
	{
		mNumMeshes = 0;
		mPos.clear();
		mPosIndices.clear();
		mNormals.clear();
		mNormalIndices.clear();
		mUVs.clear();
		mUVIndices.clear();
		mMaterials.clear();
		// different file.
		mFilepath = filepath;
		COLLADASaxFWL::Loader loader;
		successful = loader.loadDocument(filepath, this);
	}
	else
	{
		for (size_t m=0; m<mNumMeshes; m++)
			FeedGeometry(m);
		successful = true;
	}

	if (mUseMeshGroup)
	{
		mMeshGroup->SetAuxiliaries(mAuxil);
	}
	else
	{
		mMeshObjects[0]->SetAuxiliaries(mAuxil);
	}

	return successful;
}

void ColladaImporter::cancel(const COLLADAFW::String& errorMessage)
{
}

void ColladaImporter::start()
{
}

void ColladaImporter::finish()
{
}

bool ColladaImporter::writeGlobalAsset ( const COLLADAFW::FileInfo* asset )
{
	return true;
}

bool ColladaImporter::writeScene ( const COLLADAFW::Scene* scene )
{
	return true;
}

void ColladaImporter::WriteChildNode(const COLLADAFW::Node* node, size_t parent)
{
	using namespace COLLADAFW;

	std::string name = node->getName();
	COLLADABU::Math::Matrix4 mat = node->getTransformationMatrix();
	const TransformationPointerArray& ta = node->getTransformations();
	COLLADABU::Math::Vector3 scale = mat.getScale();
	COLLADABU::Math::Quaternion rot = mat.extractQuaternion();
	COLLADABU::Math::Vector3 trans = mat.getTrans();
		
	Transformation transform;
	transform.SetScale(Vec3((float)scale.x, (float)scale.y, (float)scale.z));
	transform.SetRotation(Quat((float)rot.w, (float)rot.x, (float)rot.y, (float)rot.z));
	transform.SetTranslation(Vec3((float)trans.x, (float)trans.y, (float)trans.z));
		
	const InstanceGeometryPointerArray& ga = node->getInstanceGeometries();
	size_t gaCount = ga.getCount();
	size_t idx = -1;
	for(size_t g = 0; g<gaCount; g++)
	{
		std::string id = ga[g]->getInstanciatedObjectId().toAscii();
		IMeshObject* pMeshObject = GetMeshObject(id.c_str());
		if (pMeshObject)
		{
			size_t idxTemp = mMeshGroup->AddMesh(pMeshObject, transform, parent);
			if (g==0)
				idx = idxTemp; // this is temporary.
								// Currently the engine doesn't have seperated node - object structure like ogre.
								// So we are assuming one node have one mesh. This has no problem for now.
		}
	}

	if (idx != -1)
	{
		const NodePointerArray& na = node->getChildNodes();
		size_t naCount = na.getCount();
		for (size_t n = 0; n< naCount; n++)
		{
			WriteChildNode(na[n], idx);
		}
	}

}

bool ColladaImporter::writeVisualScene ( const COLLADAFW::VisualScene* visualScene )
{
	mMeshGroup = new MeshGroup();

	using namespace COLLADAFW;
	const NodePointerArray& node = visualScene->getRootNodes();
	size_t count = node.getCount();
	for(size_t i=0; i<count; i++)
	{
		std::string name = node[i]->getName();
		COLLADABU::Math::Matrix4 mat = node[i]->getTransformationMatrix();
		COLLADABU::Math::Vector3 scale = mat.getScale();
		COLLADABU::Math::Quaternion rot = mat.extractQuaternion();
		COLLADABU::Math::Vector3 trans = mat.getTrans();
		
		Transformation transform;
		transform.SetScale(Vec3((float)scale.x, (float)scale.y, (float)scale.z));
		transform.SetRotation(Quat((float)rot.w, (float)rot.x, (float)rot.y, (float)rot.z));
		transform.SetTranslation(Vec3((float)trans.x, (float)trans.y, (float)trans.z));
		size_t idx = -1;
		// auxiliaries
		if (name.find("_POS") == 0)
		{
			mAuxil.push_back(AUXILIARIES::value_type(name, transform));
		}
		else if (mUseMeshGroup)
		{
			const InstanceGeometryPointerArray& ga = node[i]->getInstanceGeometries();
			size_t gaCount = ga.getCount();
			for(size_t g = 0; g<gaCount; g++)
			{
				std::string id = ga[g]->getInstanciatedObjectId().toAscii();
				IMeshObject* pMeshObject = GetMeshObject(id.c_str());
				if (pMeshObject)
				{
					size_t idxTemp = mMeshGroup->AddMesh(pMeshObject, transform, -1);
					if (g==0)
						idx = idxTemp; // this is temporary.
									   // Currently the engine doesn't have seperated node - object structure like ogre.
									   // So we are assuming one node have one mesh. This has no problem for now.
				}
			}
		}
		if (idx != -1 && mUseMeshGroup)
		{
			const NodePointerArray& na = node[i]->getChildNodes();
			size_t naCount = na.getCount();
			for (size_t n = 0; n< naCount; n++)
			{
				WriteChildNode(na[n], idx);
			}
		}


	}
	return true;
}

bool ColladaImporter::writeLibraryNodes ( const COLLADAFW::LibraryNodes* libraryNodes )
{
	return true;
}

bool ColladaImporter::writeGeometry ( const COLLADAFW::Geometry* geometry )
{
	using namespace COLLADAFW;
	Geometry::GeometryType type = geometry->getType();
	if (type == Geometry::GEO_TYPE_MESH || type == Geometry::GEO_TYPE_CONVEX_MESH)
	{
		Mesh* pColladaMesh = dynamic_cast<Mesh*>( const_cast<Geometry*>(geometry) );
		if (pColladaMesh)
		{
			CopyData(pColladaMesh);
			FeedGeometry(mNumMeshes-1);
		}
	}
	return true;
}

bool ColladaImporter::writeMaterial( const COLLADAFW::Material* material )
{
	return true;
}

bool ColladaImporter::writeEffect( const COLLADAFW::Effect* effect )
{
	return true;
}

bool ColladaImporter::writeCamera( const COLLADAFW::Camera* camera )
{
	return true;
}

bool ColladaImporter::writeImage( const COLLADAFW::Image* image )
{
	return true;
}

bool ColladaImporter::writeLight( const COLLADAFW::Light* light )
{
	return true;
}

bool ColladaImporter::writeAnimation( const COLLADAFW::Animation* animation )
{
	return true;
}

bool ColladaImporter::writeAnimationList( const COLLADAFW::AnimationList* animationList )
{
	return true;
}

bool ColladaImporter::writeSkinControllerData( const COLLADAFW::SkinControllerData* skinControllerData )
{
	return true;
}

bool ColladaImporter::writeController( const COLLADAFW::Controller* controller )
{
	return true;
}

bool ColladaImporter::writeFormulas( const COLLADAFW::Formulas* formulas )
{
	return true;
}

bool ColladaImporter::writeKinematicsScene( const COLLADAFW::KinematicsScene* kinematicsScene )
{
	return true;
}

void ColladaImporter::GetFloatOrDouble(FLOAT_DATA& dest, COLLADAFW::MeshVertexData& src)
{
	using namespace COLLADAFW;
	if (src.getType() == FloatOrDoubleArray::DATA_TYPE_FLOAT)
	{
		FloatArray* pFloatArray = src.getFloatValues();
		size_t numFloats = pFloatArray->getCount();
		dest.assign(pFloatArray->getData(), pFloatArray->getData()+numFloats);
	}
	else if (src.getType() == FloatOrDoubleArray::DATA_TYPE_DOUBLE)
	{
		DoubleArray* pDoubleArray = src.getDoubleValues();
		size_t numDoubles = pDoubleArray->getCount();
		dest.clear();
		dest.reserve(numDoubles);
		for (size_t i=0; i<numDoubles; i++)
		{
			dest.push_back( (float)((*pDoubleArray)[i]) );
		}
	}
}

static std::string GetMaterialFilepath(const char* sz)
{
	if (!sz || strlen(sz)==0)
		return std::string();

	// example of 'sz'
	// in case dae file exported from Blender.
	// data_materials_hull2_material-material

	struct funcObj
	{
		bool operator()( char v)
		{
			return v=='_';

		}
	};
	const char* p = strrchr(sz, '-');
	std::string ret( sz, sz + (p - sz) );
	std::replace_if(ret.begin(), ret.end(), funcObj(), '/');

	size_t dotPos = ret.find_last_of('/');
	if (dotPos != std::string::npos)
		ret[dotPos] = '.';
	else 
		return std::string();
	return ret;

}

void ColladaImporter::CopyData(COLLADAFW::Mesh* pColladaMesh)
{
	using namespace COLLADAFW;
	mNames.push_back(std::string());
	mNames.back() = pColladaMesh->getName().c_str();
	mIDs.push_back(std::string());
	mIDs.back() = pColladaMesh->getUniqueId().toAscii();
	// positions
	mPos.push_back(FLOAT_DATA());
	MeshVertexData& positions = pColladaMesh->getPositions();
	GetFloatOrDouble(mPos[mNumMeshes], positions);
	//normals
	mNormals.push_back(FLOAT_DATA());
	MeshVertexData& normals = pColladaMesh->getNormals();
	GetFloatOrDouble(mNormals[mNumMeshes], normals);
	// uvs
	mUVs.push_back(FLOAT_DATA());
	MeshVertexData& uvs = pColladaMesh->getUVCoords();
	GetFloatOrDouble(mUVs[mNumMeshes], uvs);

	MeshPrimitiveArray& meshPrimitives = pColladaMesh->getMeshPrimitives();
	size_t pc = meshPrimitives.getCount();
	mHasUVs.assign(pc, false);
	mNumPrimitives = pc;
	mPosIndices.push_back(INDICES_PRIMITIVES());
	mNormalIndices.push_back(INDICES_PRIMITIVES());
	mUVIndices.push_back(INDICES_PRIMITIVES());
	for (size_t i=0; i<pc; i++)
	{
		mPosIndices[mNumMeshes].push_back(INDICES());
		mNormalIndices[mNumMeshes].push_back(INDICES());
		mUVIndices[mNumMeshes].push_back(INDICES());
		INDICES& posIndices = mPosIndices[mNumMeshes].back();
		INDICES& normalIndices = mNormalIndices[mNumMeshes].back();
		INDICES& uvIndices = mUVIndices[mNumMeshes].back();
		MeshPrimitive::PrimitiveType type = meshPrimitives[i]->getPrimitiveType();
		switch(type)
		{
		case MeshPrimitive::POLYGONS:
		case MeshPrimitive::POLYLIST:
		case MeshPrimitive::TRIANGLES:
			{					
				UIntValuesArray& pi = meshPrimitives[i]->getPositionIndices();
				UIntValuesArray& ni = meshPrimitives[i]->getNormalIndices();
				size_t ic = pi.getCount();
				size_t ic_n = ni.getCount();				
				assert(ic==ic_n);				
				posIndices.assign(pi.getData(), pi.getData() + ic);
				normalIndices.assign(ni.getData(), ni.getData()+ic);
				
				mHasUVs[i]= meshPrimitives[i]->hasUVCoordIndices();
				if (mHasUVs[i])
				{
					UIntValuesArray& ui = meshPrimitives[i]->getUVCoordIndices(0)->getIndices();
					size_t ic_u = ui.getCount();
					assert(ic==ic_u);
					uvIndices.assign(ui.getData(), ui.getData() + ic);
				}
			}				
			break;
		default:
			Error("Cannot handle MeshPrimitive type : %d", type);
		}

		// material
		mMaterials.push_back(MATERIALS_PRIMITIVES());
		if (gFBEnv && gFBEnv->pRenderer)
		{
			if (!mMergeMaterialGroups)
			{
				for (size_t i=0; i<pc; i++)
				{
					std::string file = GetMaterialFilepath( meshPrimitives[i]->getMaterial().c_str() );
					mMaterials[mNumMeshes].push_back(IMaterial::CreateMaterial(file.c_str()));					
				}
			}
		}
	}

	mNumMeshes++;
}

//---------------------------------------------------------------------------
class vertex_hash : public std::unary_function<DEFAULT_INPUTS::V_PNT, size_t>
{	// hash functor
public:
	size_t operator()(const DEFAULT_INPUTS::V_PNT& data) const
	{	
		return murmur3_32((char*)&data, sizeof(data), murmurSeed);
	}
};

IIndexBuffer* ColladaImporter::CreateIndexBuffer(UINT* indices, size_t num)
{
	bool b32bit = num > std::numeric_limits<USHORT>::max();
	IIndexBuffer* pIndexBuffer = 0;
	if (b32bit)
	{
		if (gFBEnv)
			pIndexBuffer = gFBEnv->pRenderer->CreateIndexBuffer(indices, num, INDEXBUFFER_FORMAT_32BIT);
	}
	else
	{
		std::vector<USHORT> indices16;
		indices16.reserve(num);
		for (size_t i=0; i<num; i++)
		{
			indices16.push_back((USHORT)indices[i]);
		}
		if (gFBEnv)
			pIndexBuffer = gFBEnv->pRenderer->CreateIndexBuffer(&indices16[0], indices16.size(), INDEXBUFFER_FORMAT_16BIT);
	}

	return pIndexBuffer;
}

//---------------------------------------------------------------------------
void ColladaImporter::FeedGeometry(size_t mesh)
{
	unsigned elemOffset[] = {0, 1, 2};
	if (mSwapYZ)
	{
		std::swap(elemOffset[1], elemOffset[2]);
	}
	unsigned indexOffset[] = {0, 1, 2};
	if (mOppositeCull)
	{
		std::swap(indexOffset[1], indexOffset[2]);
	}

	if (mesh >= mMeshObjects.size())
		mMeshObjects.push_back(IMeshObject::CreateMeshObject());
	mMeshObjects.back()->SetName(mNames[mesh].c_str());
	IMeshObject* pMeshObject = mMeshObjects.back();
	if (mPosIndices[mesh].empty())
		return;	

	pMeshObject->StartModification();

	std::vector<Vec3> positions;
	positions.reserve(10000);
	std::vector<Vec3> normals;
	normals.reserve(10000);
	std::vector<Vec2> uvs;
	uvs.reserve(10000);
	std::vector<Vec3> tangents;
	tangents.reserve(10000);

	size_t nextIdx = 0;
	INDICES indices;
	indices.reserve(10000);
	std::set<DEFAULT_INPUTS::V_PNT> vertSet; // for building index buffer
	std::map<DEFAULT_INPUTS::V_PNT, size_t> vertToIdx; // for building index buffer
	for (int pri=0; pri<mNumPrimitives; pri++)
	{
		const INDICES& posIndices = mPosIndices[mesh][pri];
		const INDICES& norIndices = mNormalIndices[mesh][pri];
		const INDICES& uvIndices = mUVIndices[mesh][pri];
		size_t numIndices = posIndices.size();
		if (!numIndices)
			continue;
		assert(numIndices%3==0);
		assert(numIndices==norIndices.size());
		assert(uvIndices.size() == 0 || numIndices==uvIndices.size());	
		if (mUseIndexBuffer)
		{
			for(size_t i=0; i<numIndices; i+=3)
			{
				for (int k=0; k<3; k++)
				{
					size_t pi = posIndices[i+indexOffset[k]]*3;
					size_t ni = norIndices[i+indexOffset[k]]*3;
					Vec2 uvCoord(0, 0);
					if (mHasUVs[pri])
					{
						size_t ui = uvIndices[i+indexOffset[k]]*2;
						uvCoord = Vec2(mUVs[mesh][ui], mUVs[mesh][ui+1]);
					}
				
					DEFAULT_INPUTS::V_PNT vert(
						Vec3(mPos[mesh][pi], mPos[mesh][pi+elemOffset[1]], mPos[mesh][pi+elemOffset[2]]),
						Vec3(mNormals[mesh][ni], mNormals[mesh][ni+elemOffset[1]], mNormals[mesh][ni+elemOffset[2]]),
						uvCoord);
					
					auto ret = vertSet.insert(vert);
					if (ret.second)
					{
						// new vertex
						vertToIdx[vert] = nextIdx;
						indices.push_back(nextIdx++);
						positions.push_back(vert.p);
						normals.push_back(vert.n);
						uvs.push_back(vert.uv);
					}
					else
					{
						assert(vertToIdx.find(vert) != vertToIdx.end());
						// existing
						indices.push_back(vertToIdx[vert]);
					}
				}
			}
			if (!mMergeMaterialGroups)
			{
				IIndexBuffer* pIndexBuffer = CreateIndexBuffer(&indices[0], indices.size());
				pMeshObject->SetIndexBuffer(pri, pIndexBuffer);
				indices.clear();
				vertSet.clear();
				vertToIdx.clear();
				nextIdx = 0;
			}
		}
		else // not using index buffer
		{
			for(size_t i=0; i<numIndices; i+=3)
			{
				for (int k=0; k<3; k++)
				{
					size_t pi = posIndices[i+indexOffset[k]]*3;
					size_t ni = norIndices[i+indexOffset[k]]*3;
					Vec2 uvCoord(0, 0);
					if (mHasUVs[pri])
					{
						size_t ui = uvIndices[i+indexOffset[k]]*2;
						uvCoord = Vec2(mUVs[mesh][ui+0], mUVs[mesh][ui+1]);
					}
				
					positions.push_back(Vec3(mPos[mesh][pi], mPos[mesh][pi+elemOffset[1]], mPos[mesh][pi+elemOffset[2]]));
					normals.push_back(Vec3(mNormals[mesh][ni], mNormals[mesh][ni+elemOffset[1]], mNormals[mesh][ni+elemOffset[2]]));
					uvs.push_back(uvCoord);
				}
			}
		}

		if (!mMergeMaterialGroups)
		{
			if (positions.empty())
			{
				Log("Collada import found a geometry that has no position data.");
				continue;
			}
			size_t added = positions.size();
			assert(added == normals.size());
			assert(added == uvs.size());

			pMeshObject->SetPositions(pri, &positions[0], added);
			pMeshObject->SetNormals(pri, &normals[0], added);
			pMeshObject->SetUVs(pri, &uvs[0], added);
			positions.clear();
			normals.clear();
			uvs.clear();

			pMeshObject->SetMaterialFor(pri, mMaterials[mesh][pri]);
		}
	}

	if (mMergeMaterialGroups)
	{
		if (positions.empty())
			return;
		size_t added = positions.size();
		assert(added == normals.size());
		assert(added == uvs.size());
		pMeshObject->SetPositions(0, &positions[0], added);
		pMeshObject->SetNormals(0, &normals[0], added);
		pMeshObject->SetUVs(0, &uvs[0], added);

		if (mUseIndexBuffer)
		{
			IIndexBuffer* pIndexBuffer = CreateIndexBuffer(&indices[0], indices.size());
			pMeshObject->SetIndexBuffer(0, pIndexBuffer);
		}
	}

	if (mGenerateTangent)
	{
		size_t size = indices.size();
		pMeshObject->GenerateTangent(size ? &indices[0]: 0, size);
	}

	pMeshObject->EndModification(mKeepMeshdata);
}

IMeshObject* ColladaImporter::GetMeshObject(const char* id) const
{
	FB_FOREACH(it, mIDs)
	{
		if ((*it)==id)
		{
			size_t index = std::distance(mIDs.begin(), it);
			assert(index < mMeshObjects.size());
			return mMeshObjects[index];
		}
	}
	assert(0);
	return 0;
}

IMeshObject* ColladaImporter::GetMeshObject() const
{
	if (mMeshObjects.empty())
		return 0;

	return mMeshObjects[0];
}

}