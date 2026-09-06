// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/FirstPlayerHUDWidget.h"

#include "AbilitySystem/FirstAttributeSet.h"
#include "Character/DKCharacter.h"
#include "Components/ProgressBar.h"
#include "Components/UI/DKUIComponent.h"
#include "TimerManager.h"

void UFirstPlayerHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	ADKCharacter* Player = Cast<ADKCharacter>(GetOwningPlayerPawn());
	if (Player)
	{
		BindToPlayer(Player);
	}
	else
	{
		// 玩家还没被 Possess，下一帧重试
		RetryBindPlayer();
	}
	
}

void UFirstPlayerHUDWidget::BindToPlayer(ADKCharacter* InPlayer)
{
	if (!InPlayer)
	{
		return;
	}

	if (UDKUIComponent* DKUI = InPlayer->GetDKUIComponent())
	{
		DKUI->OnCurrentHealthChanged.AddDynamic(this, &ThisClass::HandleHealthChanged);
		DKUI->OnCurrentStaminaChanged.AddDynamic(this, &ThisClass::HandleStaminaChanged);

		// 出生时 GE 已经广播过一次（当时 HUD 还没创建），这里补读当前值。
		const UFirstAttributeSet* AttrSet = InPlayer->GetFirstAttributeSet();
		if (AttrSet)
		{
			HandleHealthChanged(AttrSet->GetMaxHealth() > 0.f ? AttrSet->GetHealth() / AttrSet->GetMaxHealth() : 0.f);
			HandleStaminaChanged(AttrSet->GetMaxStamina() > 0.f ? AttrSet->GetStamina() / AttrSet->GetMaxStamina() : 0.f);
		}
	}
}

void UFirstPlayerHUDWidget::HandleHealthChanged(float NewPercent)
{
	if (HealthBar)
	{
		HealthBar->SetPercent(NewPercent);
	}
}

void UFirstPlayerHUDWidget::HandleStaminaChanged(float NewPercent)
{
	if (StaminaBar)
	{
		StaminaBar->SetPercent(NewPercent);
	}
}

void UFirstPlayerHUDWidget::RetryBindPlayer()
{
	// HUD 已被移除/销毁时不再重试。
	if (!IsInViewport())
	{
		return;
	}

	ADKCharacter* Player = Cast<ADKCharacter>(GetOwningPlayerPawn());
	if (Player)
	{
		BindToPlayer(Player);
	}
	else
	{
		GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::RetryBindPlayer);
	}
}
