// ItemXL example, script part: every 3rd melee hit with a weapon tagged ItemXLThirdHit deals double damage.
// Which weapons have the tag is decided by YAML (itemxl\third_hit.yaml), not here.
// @wrapMethod/@addField: no vanilla file is replaced, so this works next to other script mods without merging.

@addField(CR4Player)
var itemxlThirdHitCount : int;

@wrapMethod(W3DamageManagerProcessor)
function ProcessDamageIncrease(out dmgInfos : array< SRawDamage >)
{
	var i : int;

	wrappedMethod(dmgInfos);

	if( playerAttacker && actorVictim && attackAction && action.IsActionMelee() && !action.IsDoTDamage()
		&& playerAttacker.inv.ItemHasTag( attackAction.GetWeaponId(), 'ItemXLThirdHit' ) )
	{
		playerAttacker.itemxlThirdHitCount += 1;
		if( playerAttacker.itemxlThirdHitCount >= 3 )
		{
			playerAttacker.itemxlThirdHitCount = 0;
			for( i = 0 ; i < dmgInfos.Size() ; i += 1 )
			{
				dmgInfos[i].dmgVal *= 2;
			}
		}
	}
}
