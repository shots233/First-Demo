// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/FirstBossHUDWidget.h"

#include "AbilitySystem/FirstAttributeSet.h"
#include "Character/BossCharacter.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/UI/EnemyUIComponent.h"
#include "EngineUtils.h"

void UFirstBossHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 本册单 BOSS 场景：直接找场景里第一个 BOSS。
	// 以后多 BOSS / 动态生成时，改为由玩家 HUD 传入当前锁定目标。
	for (TActorIterator<ABossCharacter> It(GetWorld()); It; ++It)
	{
		BindToBoss(*It);
		break;
	}
}

void UFirstBossHUDWidget::BindToBoss(ABossCharacter* InBoss)
{
	if (!InBoss)
	{
		return;
	}

	if (BossNameText)
	{
		BossNameText->SetText(InBoss->BossName);
	}

	if (UEnemyUIComponent* EnemyUI = InBoss->GetEnemyUIComponent())
	{
		EnemyUI->OnCurrentHealthChanged.AddDynamic(this, &ThisClass::HandleHealthChanged);
		EnemyUI->OnCurrentPoiseChanged.AddDynamic(this, &ThisClass::HandlePoiseChanged);

		// 补读当前值（出生广播发生在 HUD 创建之前）。
		const UFirstAttributeSet* AttrSet = InBoss->GetFirstAttributeSet();
		if (AttrSet)
		{
			HandleHealthChanged(AttrSet->GetMaxHealth() > 0.f ? AttrSet->GetHealth() / AttrSet->GetMaxHealth() : 0.f);
			HandlePoiseChanged(AttrSet->GetMaxPoise() > 0.f ? AttrSet->GetPoise() / AttrSet->GetMaxPoise() : 0.f);
		}
	}
}

void UFirstBossHUDWidget::HandleHealthChanged(float NewPercent)
{
	if (BossHealthBar)
	{
		BossHealthBar->SetPercent(NewPercent);
	}
}

void UFirstBossHUDWidget::HandlePoiseChanged(float NewPercent)
{
	if (BossPoiseBar)
	{
		BossPoiseBar->SetPercent(NewPercent);
	}
}
