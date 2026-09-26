#pragma once

class ABossCharacter;
class UAnimMontage;

namespace FirstBossPursuit
{
	// 前闪必须能完整播放一次，并为固定落点提供有效根位移。
	bool IsDodgeMontageUsable(const ABossCharacter* Boss, const UAnimMontage* Montage);
}
