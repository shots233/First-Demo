#pragma once

#include "CoreMinimal.h"
#include "EnhancedInputComponent.h"
#include "DataAssets/Input/DataAsset_InputConfig.h"
#include "FirstInputComponent.generated.h"

UCLASS()
class FIRST_API UFirstInputComponent : public UEnhancedInputComponent
{
    GENERATED_BODY()

public:
    template<class UserObject, typename CallbackFunc>
    void BindNativeInputAction(
        const UDataAsset_InputConfig* InputConfig,
        const FGameplayTag& InputTag,
        ETriggerEvent TriggerEvent,
        UserObject* ContextObject,
        CallbackFunc Func);

    template<class UserObject, typename PressedFunc, typename ReleasedFunc>
    void BindAbilityInputActions(
        const UDataAsset_InputConfig* InputConfig,
        UserObject* ContextObject,
        PressedFunc InputPressedFunc,
        ReleasedFunc InputReleasedFunc);
};

template<class UserObject, typename CallbackFunc>
void UFirstInputComponent::BindNativeInputAction(
    const UDataAsset_InputConfig* InputConfig,
    const FGameplayTag& InputTag,
    const ETriggerEvent TriggerEvent,
    UserObject* ContextObject,
    CallbackFunc Func)
{
    // InputConfig 是整个标签路由的入口，缺失属于必须修复的开发错误。
    checkf(InputConfig, TEXT("InputConfig 为空，无法绑定输入"));

    // 先由标签找到 IA，再调用 Enhanced Input 原生 BindAction。
    if (UInputAction* FoundAction =
        InputConfig->FindNativeInputActionByTag(InputTag))
    {
        BindAction(FoundAction, TriggerEvent, ContextObject, Func);
    }
}

template<class UserObject, typename PressedFunc, typename ReleasedFunc>
void UFirstInputComponent::BindAbilityInputActions(
    const UDataAsset_InputConfig* InputConfig,
    UserObject* ContextObject,
    PressedFunc InputPressedFunc,
    ReleasedFunc InputReleasedFunc)
{
    checkf(InputConfig, TEXT("InputConfig 为空，无法绑定能力输入"));

    for (const FFirstInputActionConfig& ActionConfig
        : InputConfig->AbilityInputActions)
    {
        if (!ActionConfig.IsValid())
        {
            continue;
        }

        // Started 对应按下；把 InputTag 作为额外参数一并传给角色回调。
        BindAction(ActionConfig.InputAction.Get(), ETriggerEvent::Started,
            ContextObject, InputPressedFunc, ActionConfig.InputTag);

        // Completed/Canceled 都视为释放，避免输入被中断后能力保持按住状态。
        BindAction(ActionConfig.InputAction.Get(), ETriggerEvent::Completed,
            ContextObject, InputReleasedFunc, ActionConfig.InputTag);

        BindAction(ActionConfig.InputAction.Get(), ETriggerEvent::Canceled,
            ContextObject, InputReleasedFunc, ActionConfig.InputTag);
    }
}