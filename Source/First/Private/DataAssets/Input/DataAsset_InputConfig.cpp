// Fill out your copyright notice in the Description page of Project Settings.


#include "DataAssets/Input/DataAsset_InputConfig.h"

UInputAction* UDataAsset_InputConfig::FindNativeInputActionByTag(const FGameplayTag& InputTag, bool bLogNotFound) const
{
	// 数组规模很小，线性查找简单直观；现阶段无需额外建立哈希表。
	for (const FFirstInputActionConfig& ActionConfig : NativeInputActions)
	{
		if (ActionConfig.InputAction && ActionConfig.InputTag.MatchesTagExact(InputTag))
		{
			return ActionConfig.InputAction.Get();
		}
	}
	
	if (bLogNotFound)
	{
		// 配置错误应尽早暴露，否则表现只是“按键没反应”，很难定位。
		UE_LOG(LogTemp, Error,
			TEXT("InputConfig [%s] 中没有标签 [%s] 对应的 Native InputAction"),
			*GetNameSafe(this),
			*InputTag.ToString());
	}
	
	return nullptr;
}
