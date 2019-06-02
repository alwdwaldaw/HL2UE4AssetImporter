#pragma once

#include "ModuleManager.h"
#include "AssetRegistryModule.h"

#include "IHL2Editor.h"

class UTexture;
class UVMTMaterial;
class UMaterial;

class HL2EditorImpl : public IHL2Editor
{
public:

	/** Begin IModuleInterface implementation */
	void StartupModule();
	void ShutdownModule();
	/** End IModuleInterface implementation */

private:

	const FString hl2BasePath = "/Game/hl2/";
	const FString pluginBasePath = "/HL2AssetImporter/";

	const FString hl2TextureBasePath = hl2BasePath + "materials/";
	const FString hl2MaterialBasePath = hl2BasePath + "materials/";
	const FString hl2ShaderBasePath = pluginBasePath + "Shaders/";

	const FString hl2MaterialPostfix = "_Mat";

	bool isLoading;

	FDelegateHandle handleFilesLoaded;
	FDelegateHandle handleAssetAdded;

public:

	virtual FName HL2TexturePathToAssetPath(const FString& hl2TexturePath) const override;
	virtual FName HL2MaterialPathToAssetPath(const FString& hl2MaterialPath) const override;
	virtual FName HL2ShaderPathToAssetPath(const FString& hl2ShaderPath) const override;
	
	virtual UTexture* TryResolveHL2Texture(const FString& hl2TexturePath) const override;
	virtual UVMTMaterial* TryResolveHL2Material(const FString& hl2TexturePath) const override;
	virtual UMaterial* TryResolveHL2Shader(const FString& hl2ShaderPath) const override;
	
	virtual void FindAllMaterialsThatReferenceTexture(const FString& hl2TexturePath, TArray<UVMTMaterial*>& out) const override;

private:

	void HandleFilesLoaded();
	void HandleAssetAdded(const FAssetData& assetData);

};