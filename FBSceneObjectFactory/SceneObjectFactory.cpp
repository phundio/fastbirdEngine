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
#include "SceneObjectFactory.h"
#include "FBCommonHeaders/Helpers.h"
#include "FBStringLib/StringLib.h"
#include "FBDebugLib/Logger.h"
#include "FBColladaImporter/ColladaImporter.h"
#include "FBRenderer/Renderer.h"
#include "FBRenderer/ResourceProvider.h"
#include "FBAnimation/AnimationData.h"
#include "FBAnimation/Animation.h"
#include "FBTimer/Timer.h"
#include "FBMathLib/GeomUtils.h"
#include "FBFileSystem/FileSystem.h"
#include "MeshObject.h"
#include "MeshGroup.h"
#include "SkySphere.h"
#include "SkyBox.h"
#include "BillboardQuad.h"
#include "DustRenderer.h"
#include "TrailObject.h"
#include "binary_mesh.h"
#include "SceneObjectFactoryOptions.h"

#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/archive/binary_iarchive.hpp>

using namespace fb;

class SceneObjectFactory::Impl
{
public:
	SceneObjectFactoryWeakPtr mSelfPtr;
	// T itself should be shared_ptr
	template <class T>
	struct DataHolder{
		unsigned mNumCloned;
		T mObject;
	};
	// key is lower case.
	std::unordered_map<std::string, DataHolder<MeshObjectPtr> > mMeshObjects;
	std::unordered_map<std::string, DataHolder<MeshGroupPtr> > mMeshGroups;
	std::unordered_map<std::string, std::vector< MeshObjectPtr >  > mFractureObjects;
	bool mNoMesh;
	SceneObjectFactoryOptionsPtr mOptions;

	SkySphereWeakPtr mNextEnvUpdateSky;
	Impl()
		: mNoMesh(false)
	{		
		mOptions = SceneObjectFactoryOptions::Create();
	}
	~Impl(){
	}

	void SetEnableMeshLoad(bool enable){
		mNoMesh = !enable;
	}

	std::vector<ModelTriangle> ConvertCollada(const std::vector<collada::ModelTriangle>& data){
		std::vector<ModelTriangle> ret;
		ret.reserve(data.size());
		for (auto src : data){
			ret.push_back(ModelTriangle());
			auto& d = ret.back();
			for (int i = 0; i<3; ++i) 
				d.v[i] = src.v[i];
			d.v0Proj = src.v0Proj;
			d.v1Proj = src.v1Proj;
			d.v2Proj = src.v2Proj;
			d.faceNormal = src.faceNormal;
			d.d = src.d;
			d.dominantAxis = src.dominantAxis;
		}
		return ret;
	}

	COLLISION_INFOS ConvertCollada(const collada::COLLISION_INFOS& colInfos){
		COLLISION_INFOS ret;
		for (auto& it : colInfos){
			ret.push_back(CollisionInfo());
			auto& d = ret.back();
			d.mColShapeType = (ColisionShapeType::Enum)it.mColShapeType;
			d.mTransform = it.mTransform;
			d.mCollisionMesh = ConvertMeshData(it.mCollisionMesh, "", false, true);			
		}
		return ret;
	}

	MeshCamera ConvertCollada(const collada::CameraInfo& camInfo){
		MeshCamera ret;
		ret.mAspectRatio = camInfo.mData.mAspectRatio;
		ret.mFar = camInfo.mData.mFar;
		ret.mLocation = camInfo.mLocation;
		Quat rot(Radian(-90), Vec3::UNIT_X);
		ret.mLocation.AddRotation(rot);
		ret.mName = camInfo.mName;
		ret.mNear = camInfo.mData.mNear;
		if (camInfo.mData.mXFov > 0.f){
			// convert to y;
			ret.mFov = Radian(camInfo.mData.mXFov / ret.mAspectRatio);
		}
		else if (camInfo.mData.mYFov > 0.f){
			ret.mFov = Radian(camInfo.mData.mYFov);
		}
		else{
			Logger::Log(FB_ERROR_LOG_ARG, FormatString(
				"Collada camera info(%s) doesn't have fov", camInfo.mName.c_str()).c_str());
		}
		
		return ret;
	}

	MaterialPtr GetFallbackMaterial(const char* originalPath, const char* daePath){
		auto materialFileName = FileSystem::GetFileName(originalPath);
		auto path = FileSystem::GetParentPath(daePath);
		path += "/";
		auto ret = FileSystem::ConcatPath(path.c_str(), materialFileName.c_str());
		return Renderer::GetInstance().CreateMaterial(ret.c_str());
	}

	MeshObjectPtr ConvertMeshData(collada::MeshPtr meshData, const char* daeFilepath, bool buildTangent, bool keepDataInMesh){
		if (!meshData)
			return 0;

		auto mesh = MeshObject::Create();
		mesh->SetName(meshData->mName.c_str());
		mesh->StartModification();
		for (auto& group : meshData->mMaterialGroups){
			std::vector<Vec3> data = group.second.mPositions;
			mesh->SetPositions(group.first, &data[0], data.size());
			if (!group.second.mNormals.empty()){
				data = group.second.mNormals;
				mesh->SetNormals(group.first, &data[0], data.size());
			}
			if (!group.second.mUVs.empty()){
				std::vector<Vec2> data2 = group.second.mUVs;
				mesh->SetUVs(group.first, &data2[0], data2.size());
			}
			if (!group.second.mTriangles.empty()){
				std::vector<ModelTriangle> triangles = ConvertCollada(group.second.mTriangles);
				mesh->SetTriangles(group.first, &triangles[0], triangles.size());
			}
			if (!group.second.mIndexBuffer.empty()) {				
				mesh->SetIndices(group.first, group.second.mIndexBuffer);
			}
			MaterialPtr material;
			if (!group.second.mMaterialPath.empty()){
				if (FileSystem::ResourceExists(group.second.mMaterialPath.c_str())){
					material = Renderer::GetInstance().CreateMaterial(group.second.mMaterialPath.c_str());
				}
				else{
					material = GetFallbackMaterial(group.second.mMaterialPath.c_str(), daeFilepath);
				}
			}
			else{
				material = Renderer::GetInstance().GetResourceProvider()->GetMaterial(
					ResourceTypes::Materials::Missing);
			}
			mesh->SetMaterialFor(group.first, material);
			if (buildTangent){
				if (group.second.mIndexBuffer.empty()){
					mesh->GenerateTangent(group.first, 0, 0);
				}
				else{
					mesh->GenerateTangent(group.first, &group.second.mIndexBuffer[0], group.second.mIndexBuffer.size());
				}
			}
		}
		mesh->EndModification(keepDataInMesh);

		if (meshData->mAnimationData){			
			auto animation = Animation::Create();
			animation->SetAnimationData(meshData->mAnimationData);
			mesh->SetAnimation(animation);
		}

		for (auto& it : meshData->mAuxiliaries){
			AUXILIARY aux;
			aux.first = it.first;			
			aux.second = it.second;
			mesh->AddAuxiliary(aux);
		}
		mesh->SetCollisionShapes(ConvertCollada(meshData->mCollisionInfo));

		MeshCameras cameras;
		if (!meshData->mCameraInfo.empty()){
			MeshCameras cameras;
			for (auto& it : meshData->mCameraInfo){
				MeshCamera cam = ConvertCollada(it.second);
				cameras.insert(std::make_pair(it.first, cam));				
			}
			mesh->SetMeshCameras(cameras);
		}

		return mesh;
	}

	MeshObjectPtr CreateMeshObject(const char* daeFilePath){
		return CreateMeshObject(daeFilePath, MeshImportDesc());
	}

	MeshObjectPtr CreateMeshObject(const char* daeFilePath, const MeshImportDesc& desc){
		if (!ValidCString(daeFilePath)) {
			Logger::Log(FB_ERROR_LOG_ARG, "Invalid arg.");
			return 0;
		}
		std::string filepath(daeFilePath);
		if (mNoMesh)
		{
			filepath = "EssentialEngineData/objects/defaultCube.dae";
		}

		std::string filepathKey = filepath;
		ToLowerCase(filepathKey);
		auto it = mMeshObjects.find(filepathKey);
		if (it != mMeshObjects.end()) {
			it->second.mNumCloned++;
			return it->second.mObject->Clone();
		}
		collada::MeshPtr compiled_mesh;
		std::string fbmesh_path = FileSystem::ReplaceExtension(daeFilePath, "fbmesh");
		if (!mOptions->o_rawCollada) {			
			FileSystem::Open file(fbmesh_path.c_str(), "rb");
			if (file.IsOpen()) {
				auto& data = *file.GetBinaryData();
				typedef boost::iostreams::basic_array_source<char> Device;
				boost::iostreams::stream_buffer<Device> buffer((char*)&data[0], data.size());
				std::istream stream(&buffer);
				compiled_mesh = load_mesh(stream, fbmesh_path.c_str(), desc);
			}
		}

		if (!compiled_mesh) {
			Logger::Log(FB_DEFAULT_LOG_ARG, FormatString(
				"(info) %s not found. Trying to load .dae file.", fbmesh_path.c_str()).c_str());

			auto pColladaImporter = ColladaImporter::Create();
			ColladaImporter::ImportOptions option;
			option.mMergeMaterialGroups = desc.mergeMaterialGroups;
			option.mOppositeCull = desc.oppositeCull;
			option.mSwapYZ = desc.yzSwap;
			option.mUseIndexBuffer = desc.useIndexBuffer;
			option.mUseMeshGroup = false;
			pColladaImporter->ImportCollada(filepath.c_str(), option);
			compiled_mesh = pColladaImporter->GetMeshObject();
		}
		auto meshObject = ConvertMeshData(compiled_mesh, daeFilePath, desc.generateTangent, desc.keepMeshData);
		if (meshObject)
		{
			meshObject->SetName(filepath.c_str());
			mMeshObjects[filepathKey] = DataHolder < MeshObjectPtr > {1, meshObject};
			return meshObject->Clone();
		}
		else
		{
			if (mNoMesh){
				Logger::Log(FB_ERROR_LOG_ARG, FormatString("Failed to load a mesh(%s, %s)", filepath.c_str(), daeFilePath).c_str());
			}
			else{
				Logger::Log(FB_ERROR_LOG_ARG, FormatString("Failed to load a mesh(%s)", filepath.c_str()).c_str());
			}
			return 0;
		}
	}

	std::vector<MeshObjectPtr> CreateMeshObjects(const char* daeFilePath, const MeshImportDesc& desc){
		std::vector<MeshObjectPtr> ret;
		if (!ValidCString(daeFilePath)) {
			Logger::Log(FB_ERROR_LOG_ARG, "Invalid arg.");
			return ret;
		}
		std::string filepath(daeFilePath);
		if (mNoMesh)
		{
			filepath = "EssentialEngineData/objects/defaultCube.dae";
		}
		std::string filepathKey = filepath;
		ToLowerCase(filepathKey);

		auto it = mFractureObjects.find(filepathKey);
		if (it != mFractureObjects.end()){
			for (auto mesh : it->second){
				ret.push_back(mesh->Clone());
			}
			return ret;
		}

		auto& fractureObjects = mFractureObjects[filepathKey];
		std::string fbmesh_path = FileSystem::ReplaceExtension(daeFilePath, "fbmeshes");
		std::vector<collada::MeshPtr> meshes;
		if (!mOptions->o_rawCollada) {
			FileSystem::Open file(fbmesh_path.c_str(), "rb");
			if (file.IsOpen()) {
				auto& data = *file.GetBinaryData();
				typedef boost::iostreams::basic_array_source<char> Device;
				boost::iostreams::stream_buffer<Device> buffer((char*)&data[0], data.size());
				meshes = load_meshes(std::istream(&buffer), fbmesh_path.c_str(), desc);
			}
		}

		if (meshes.empty()) {
			Logger::Log(FB_DEFAULT_LOG_ARG, FormatString(
				"(info) %s not found", fbmesh_path.c_str()).c_str());

			auto pColladaImporter = ColladaImporter::Create();
			ColladaImporter::ImportOptions option;
			option.mMergeMaterialGroups = desc.mergeMaterialGroups;
			option.mOppositeCull = desc.oppositeCull;
			option.mSwapYZ = desc.yzSwap;
			option.mUseIndexBuffer = desc.useIndexBuffer;
			option.mUseMeshGroup = false;
			pColladaImporter->ImportCollada(filepath.c_str(), option);
			auto num = pColladaImporter->GetNumMeshes();
			meshes.reserve(num);
			auto meshIt = pColladaImporter->GetMeshIterator();
			if (!meshIt.HasMoreElement()) {
				Logger::Log(FB_ERROR_LOG_ARG, FormatString("Failed to load fracture mehses(%s)", daeFilePath).c_str());
			}
			while (meshIt.HasMoreElement()) {
				meshes.push_back(meshIt.GetNext().second);				
			}
		}
		for (auto& mesh : meshes) {
			auto meshObject = ConvertMeshData(mesh, daeFilePath, desc.generateTangent, desc.keepMeshData);
			if (meshObject) {
				fractureObjects.push_back(meshObject);
			}
		}

		for (auto mesh : fractureObjects){
			ret.push_back(mesh->Clone());
		}
		return ret;
	}

	MeshObjectConstPtr GetMeshArcheType(const char* name){
		if (!ValidCString(name)){
			Logger::Log(FB_ERROR_LOG_ARG, "Invalid Arg");
			return 0;
		}
		std::string filepathKey(name);
		ToLowerCase(filepathKey);
		auto it = mMeshObjects.find(filepathKey);
		if (it != mMeshObjects.end()){
			return it->second.mObject;
		}
		return 0;
	}

	MeshGroupPtr ConvertMeshGroupData(collada::MeshGroupPtr groupData, const char* daeFilepath, bool buildTangent, bool keepDataInMesh){
		MeshGroupPtr meshGroup = MeshGroup::Create();
		for (auto& it : groupData->mMeshes){
			auto mesh = ConvertMeshData(it.second.mMesh, daeFilepath, buildTangent, keepDataInMesh);
			auto transformation = it.second.mTransformation;
			auto animData = mesh->GetAnimationData();
			
			unsigned idx = meshGroup->AddMesh(mesh, transformation, it.second.mParentMeshIdx);
			if (animData){
				Transformation toLocal = meshGroup->GetToLocalTransform(idx);
				animData->ApplyTransform(toLocal);
			}
		}

		for (auto& it : groupData->mAuxiliaries){
			AUXILIARY aux;
			aux.first = it.first;
			aux.second = it.second;
			meshGroup->AddAuxiliary(aux);
		}
		meshGroup->SetCollisionShapes(ConvertCollada(groupData->mCollisionInfo));

		if (!groupData->mCameraInfo.empty()){
			MeshCameras cameras;
			for (auto& it : groupData->mCameraInfo){
				MeshCamera cam = ConvertCollada(it.second);
				cameras.insert(std::make_pair(it.first, cam));
			}
			meshGroup->SetMeshCameras(cameras);
		}
		return meshGroup;
	}

	MeshGroupPtr CreateMeshGroup(const char* file){
		return CreateMeshGroup(file, MeshImportDesc());
	}

	MeshGroupPtr CreateMeshGroup(const char* daeFilePath, const MeshImportDesc& desc){
		if (!ValidCString(daeFilePath)) {
			Logger::Log(FB_ERROR_LOG_ARG, "Invalid arg.");
			return 0;
		}
		std::string filepath(daeFilePath);
		if (mNoMesh)
		{
			filepath = "EssentialEngineData/objects/defaultCube.dae";
		}
		std::string filepathKey = filepath;
		ToLowerCase(filepathKey);

		auto it = mMeshGroups.find(filepathKey);
		if (it != mMeshGroups.end()){
			it->second.mNumCloned++;
			return it->second.mObject->Clone();
		}

		collada::MeshGroupPtr compiled_mesh;
		std::string fbmesh_path = FileSystem::ReplaceExtension(daeFilePath, "fbmesh_group");		
		if (!mOptions->o_rawCollada) {
			FileSystem::Open file(fbmesh_path.c_str(), "rb");
			if (file.IsOpen()) {
				auto& data = *file.GetBinaryData();
				typedef boost::iostreams::basic_array_source<char> Device;
				boost::iostreams::stream_buffer<Device> buffer((char*)&data[0], data.size());
				compiled_mesh = load_mesh_group(std::istream(&buffer), fbmesh_path.c_str(), desc);
			}
		}
		if (!compiled_mesh) {
			Logger::Log(FB_DEFAULT_LOG_ARG, FormatString(
				"(info) %s does not exist.", fbmesh_path.c_str()).c_str());
			auto pColladaImporter = ColladaImporter::Create();
			ColladaImporter::ImportOptions option;
			option.mMergeMaterialGroups = desc.mergeMaterialGroups;
			option.mOppositeCull = desc.oppositeCull;
			option.mSwapYZ = desc.yzSwap;
			option.mUseIndexBuffer = desc.useIndexBuffer;
			option.mUseMeshGroup = true;
			pColladaImporter->ImportCollada(filepath.c_str(), option);
			compiled_mesh = pColladaImporter->GetMeshGroup();
		}
		auto meshGroup = ConvertMeshGroupData(compiled_mesh, daeFilePath, desc.generateTangent, desc.keepMeshData);
		if (meshGroup)
		{
			meshGroup->SetName(filepath.c_str());
			mMeshGroups[filepathKey] = DataHolder < MeshGroupPtr > {1, meshGroup};
			return meshGroup->Clone();
		}
		else
		{
			if (mNoMesh){
				Logger::Log(FB_ERROR_LOG_ARG, FormatString("Failed to load a mesh group(%s, %s)", filepath.c_str(), daeFilePath).c_str());
			}
			else{
				Logger::Log(FB_ERROR_LOG_ARG, FormatString("Failed to load a mesh group(%s)", filepath.c_str()).c_str());
			}
			return 0;
		}
	}

	SkySpherePtr CreateSkySphere(){
		return SkySphere::Create();
	}

	SkyBoxPtr CreateSkyBox(const char* materialPath){
		return SkyBox::Create(materialPath);
	}

	BillboardQuadPtr CreateBillboardQuad(){
		return BillboardQuad::Create();
	}

	DustRendererPtr CreateDustRenderer(){
		return DustRenderer::Create();
	}

	TrailObjectPtr CreateTrailObject(){
		return TrailObject::Create();
	}

	void UpdateEnvMapInNextFrame(SkySpherePtr sky){
		mNextEnvUpdateSky = sky;
	}

	void Update(TIME_PRECISION dt){
		auto sky = mNextEnvUpdateSky.lock();
		if (sky){
			mNextEnvUpdateSky.reset();
			sky->UpdateEnvironmentMap(Vec3(0, 0, 0));
		}
	}
};

//---------------------------------------------------------------------------
SceneObjectFactoryWeakPtr sSceneObjectFactory;
SceneObjectFactoryPtr SceneObjectFactory::Create(){
	if (sSceneObjectFactory.expired()){
		auto p = SceneObjectFactoryPtr(new SceneObjectFactory, [](SceneObjectFactory* obj){ delete obj; });
		sSceneObjectFactory = p;
		p->mImpl->mSelfPtr = p;
		return p;
	}
	return sSceneObjectFactory.lock();
}
SceneObjectFactory& SceneObjectFactory::GetInstance(){
	auto p = sSceneObjectFactory.lock();
	if (!p){
		Logger::Log(FB_ERROR_LOG_ARG, "SceneObjectFactory is deleted. Program will crash.");
	}
	return *p;
}

SceneObjectFactory::SceneObjectFactory()
	: mImpl(new Impl)
{
}

SceneObjectFactory::~SceneObjectFactory(){
	ColladaImporter::CleanUP();
}

void SceneObjectFactory::SetEnableMeshLoad(bool enable) {
	mImpl->SetEnableMeshLoad(enable);
}

MeshObjectPtr SceneObjectFactory::CreateMeshObject(const char* daeFilePath) {
	return mImpl->CreateMeshObject(daeFilePath);
}

MeshObjectPtr SceneObjectFactory::CreateMeshObject(const char* daeFilePath, const MeshImportDesc& desc) {
	return mImpl->CreateMeshObject(daeFilePath, desc);
}

std::vector<MeshObjectPtr> SceneObjectFactory::CreateMeshObjects(const char* daeFilePath, const MeshImportDesc& desc){
	return mImpl->CreateMeshObjects(daeFilePath, desc);
}

MeshObjectConstPtr SceneObjectFactory::GetMeshArcheType(const char* name) {
	return mImpl->GetMeshArcheType(name);
}

MeshGroupPtr SceneObjectFactory::CreateMeshGroup(const char* file) {
	return mImpl->CreateMeshGroup(file);
}

MeshGroupPtr SceneObjectFactory::CreateMeshGroup(const char* file, const MeshImportDesc& desc) {
	return mImpl->CreateMeshGroup(file, desc);
}

SkySpherePtr SceneObjectFactory::CreateSkySphere() {
	return mImpl->CreateSkySphere();
}

SkyBoxPtr SceneObjectFactory::CreateSkyBox(const char* materialPath){
	return mImpl->CreateSkyBox(materialPath);
}

BillboardQuadPtr SceneObjectFactory::CreateBillboardQuad() {
	return mImpl->CreateBillboardQuad();
}

DustRendererPtr SceneObjectFactory::CreateDustRenderer() {
	return mImpl->CreateDustRenderer();
}

TrailObjectPtr SceneObjectFactory::CreateTrailObject() {
	return mImpl->CreateTrailObject();
}

void SceneObjectFactory::UpdateEnvMapInNextFrame(SkySpherePtr sky) {
	mImpl->UpdateEnvMapInNextFrame(sky);
}

void SceneObjectFactory::Update(TIME_PRECISION dt) {
	mImpl->Update(dt);
}

