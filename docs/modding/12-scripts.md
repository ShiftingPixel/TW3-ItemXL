# Scripts

ItemXL changes data. Behavior lives in WitcherScript. The two work together best when the YAML decides **which**
items have a behavior and the script decides **what** it does.

## Tags connect YAML and scripts

Give the items a tag in YAML:

```yaml
item:
  My Steel Sword:
    tags: !append [MyThirdHit]
```

and let the script look for the tag, not for item names:

```witcherscript
@addField(CR4Player)
var myThirdHitCount : int;

@wrapMethod(W3DamageManagerProcessor)
function ProcessDamageIncrease(out dmgInfos : array< SRawDamage >)
{
	var i : int;

	wrappedMethod(dmgInfos);

	if( playerAttacker && attackAction && action.IsActionMelee()
		&& playerAttacker.inv.ItemHasTag( attackAction.GetWeaponId(), 'MyThirdHit' ) )
	{
		playerAttacker.myThirdHitCount += 1;
		if( playerAttacker.myThirdHitCount >= 3 )
		{
			playerAttacker.myThirdHitCount = 0;
			for( i = 0 ; i < dmgInfos.Size() ; i += 1 )
			{
				dmgInfos[i].dmgVal *= 2;
			}
		}
	}
}
```

Other mods can then give the behavior to their own items with one YAML line, or take it away with `!remove`.
The complete example is in [`examples/modItemXLThirdHit`](../../examples/modItemXLThirdHit).

The script goes into `Mods\<mod>\content\scripts\local\`.

## Use annotations wherever possible

A mod that replaces a whole vanilla script file collides with every other mod that replaces the same file, and players
then have to merge scripts themselves. Annotations change single functions and fields of a class instead, and many mods
can do that to the same class at once. Use them whenever possible:

| Annotation | Effect |
|---|---|
| `@addField(Class)` | adds a variable to a class (`saved var` keeps it in the save) |
| `@addMethod(Class)` | adds a function to a class |
| `@wrapMethod(Class)` | wraps an existing function: your code runs around it, `wrappedMethod(...)` calls the original; several mods can wrap the same function |
| `@replaceMethod(Class)` | replaces an existing function; only one mod can do that per function, so prefer `@wrapMethod` |

Since the Remastered patch (5.0), events and states can be annotated as well, and native functions can be wrapped. A
wrapped `event` has no return type in its declaration (it always returns `bool`). States are annotated with the
state's class name, `<Parent>State<Name>`, for example `@wrapMethod(WeaponHolsterStateSelectingWeapon)`.

## Item names in scripts

A name literal in WitcherScript (`'Long Steel Sword'`) may hold letters, digits, and spaces, but no brackets or other
signs, and WitcherScript has no `StringToName` or similar conversion. An item called `My Sword (Steel)` cannot be named in
a script directly. Either choose names without such signs for items that scripts use, or find them by tag:

```witcherscript
var names : array<name>;
names = theGame.GetDefinitionsManager().GetItemsWithTag('MyThirdHit');
```

## Reading values

Scripts read the values the game loaded, which are ItemXL's result. For example
`theGame.GetDefinitionsManager().ItemHasTag(itemName, 'MyTag')` or the inventory functions on an item. Values that a
script changes at runtime are outside ItemXL; the report does not see them.

## Effects tied to item tags

Some vanilla behavior is bound to tags. Aerondight's buff, for example, follows the tag `Aerondight` but is only
checked for the silver sword slot. A copy moved to the steel slot keeps the tag but needs a script that runs the same
checks for that slot. When you copy an item with `$base`, check whether its behavior depends on the slot or category
you change.

Next: [Troubleshooting](13-troubleshooting.md).
