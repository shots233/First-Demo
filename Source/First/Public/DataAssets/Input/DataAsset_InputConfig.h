// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "DataAsset_InputConfig.generated.h"


class UInputMappingContext;
class UInputAction;

USTRUCT(BlueprintType)
struct FFirstInputActionConfig
{
	GENERATED_BODY()
	
	// 用标签描述输入的语义，例如 InputTag.Move。
	// C++ 只依赖标签，不直接依赖键盘上的 W、Space 等具体按键。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(categories= "InputTag"))
	FGameplayTag InputTag;
	
	// 实际执行 Enhanced Input 的输入资产，例如 IA_Move。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<UInputAction> InputAction = nullptr;
	
	bool IsValid() const
	{
		// TObjectPtr 传给 UObject 的 IsValid 时需要先通过 Get() 取出裸指针。
		return InputTag.IsValid() && InputAction;
	}
	
};

/**
 * 
 */
UCLASS(BlueprintType,const)
class FIRST_API UDataAsset_InputConfig : public UDataAsset
{
	GENERATED_BODY()
	
public:
	// 角色被玩家控制时加入 LocalPlayerSubsystem 的默认映射环境。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;
	
	// Move、Look、Jump：由 Character 直接处理，不经过 GAS。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input",meta=(TitleProperty="InputTag"))
	TArray<FFirstInputActionConfig> NativeInputActions;
	
	// Attack、Dodge 等：只负责产生标签，之后交给 ASC 激活能力。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input",meta=(TitleProperty="InputTag"))
	TArray<FFirstInputActionConfig> AbilityInputActions;
	
	UInputAction* FindNativeInputActionByTag(const FGameplayTag& InputTag, bool bLogNotFound = true)const;
	
};
