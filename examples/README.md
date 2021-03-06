# Config Syntax

## Starting the Game

### GameProcessName

The game process name must always be located in the first line.
The PID is searched by name with `pidof <GameProcessName> | cut -d ' ' -f 1`.
This ensures always attaching to the most recently started instance.

### [ game_call GameProcessCall ]

Sometimes a game is called from a shell script and the call doesn't match
the GameProcessName. For this case this optional config entry is important
(example: "sauerbraten" shell script as it calls "sauer_client"). Without
this the GameProcessName is assumed.

### [ game_path AbsGamePath ]

The absolute game path is optional and must end with the GameProcessCall.
It is used to run the game. Without this it is determined with `which`
from the GameProcessCall.

### [ game_binpath GameBinaryPath ]

The absolute game binary path is used for discovery as well as adaption.
It is optional and must end with the GameProcessName. Without this the
AbsGamePath is assumed. This is only important if the GameProcessCall is
configured.

### [ game_params CommandLineOptions ]

Sometimes it is necessary to add some game command line parameters/options
and this is the optional config entry to set them.


## General Settings

### [ define MacroName LineSubstitute ]

Ugtrain has single-line macro support. This means that lines containing only
the MacroName get replaced by the LineSubstitute during parsing. This is
especially useful for repetitive checks.

### [ dynmemfile AbsoluteFilePath ]

Sometimes `/tmp/memhack_file` containing all memory allocations recorded during
dynamic memory discovery is too big for tmpfs. Then it has to be moved to a
different location. This is especially required for embedded systems and
smartphones.
TODO: Move this to a global config.

### [ use_gbt ]

This configures that GNU backtrace() is used instead of the reverse stack
offset to detect if certain dynamic memory objects are allocated. This option
should be used for **testing only** as GNU backtrace() is slow and unreliable.


## Static Memory

### < Values + checks >

### ValName AbsAddress DataType [optional check] WishValue KeyBindings ActState
### ValName AbsAddress DataType [optional check] watch

All addresses and offsets are in HEX starting with "0x" (e.g. 0xdeadbeef).

Possible DataTypes: i8, i16, i32, i64, u8, u16, u32, u64, f32, f64

checks: 'l' = "less than", 'g' = "greater than", '' = unchecked

Static wish values are in decimal (e.g. 42). But the dynamic wish values
'min', 'max' and 'watch' are also possible. 'min' and 'max' start with 0
and if the value gets less than min or greater than max, they are frozen
at that value. This is good to find out the possible limits within the
game. 'watch' makes KeyBindings and ActState useless. The line ends here
then and the value is watched but never frozen. Another possibility is
to use an absolute address or in dynamic memory an offset in the object
(e.g. 0xdeadbeef). Then, the wish value is read from that location each
time before checking and freezing it. The name of another configured and
already parsed value is the last possibility. Its value is used then or
0 in case that this value hasn't been read, yet.

KeyBindings are alphanumeric chars [0-9a-zA-Z] separated by ','. These are
processed by non-blocking getch and toggle the activation state at
runtime. Multiple keys for the same value and multiple values for the same
key are possible.

The activation state can be directly set to active ('a') or deactivated
('d') upon trainer start in the last colomn. Also watch ('w') is possible
which results in ignoring the wish value and just displaying the current
value. The difference here is that we can toggle the watching between on
and off.

### check AbsAddress DataType <'l', 'g' or 'e'> CheckValue [further 'or' checks]

Checks are applied to the previously defined value entry. Here it is also
possible to use another already parsed value name as the AbsAddress.
'e' for "equals" is another possible check here and the same value can be
compared combined with 'or' and upto four checks (e.g. "e 1 e 2" means
(Value == 1 || Value == 2)). Checks in following lines are combined with
'and'.


## Dynamic Memory

Dynamic memory cheating is used for objects on the heap allocated by a
memory allocation function like malloc() or a C++ constructor.

### dynmemstart ObjName ObjSize CodeJumpBackAddrOfMalloc RevOffsOfCodeAddrOnStack

The object size is in decimal. The rest are addresses starting with "0x".
These come from the dynamic memory discovery (libmemdisc) or from the
dynamic memory adaption.

### < Values + checks like in static memory but with ObjOffset as Addr >

### ValName OffsInObj DataType [optional check] WishValue KeyBindings ActState
### ValName OffsInObj DataType [optional check] watch

### ValName OffsInObj p PtrMemName <'once' or 'always'>

In dynamic memory also pointers are possible. These have the DataType
'p' followed by the name of an already parsed pointer memory section
and 'once' or 'always'. It chooses when to follow the pointer. E.g.
never changing pointer memory has to be processed only once directly
at the location of the configured pointer. Pointers must be settled,
meaning having the same value != NULL in two cycles so that it is safe
following this pointer.

### check OffsInObj DataType <'l', 'g' or 'e'> CheckValue [further 'or' checks]

### checko OffsInObj DataType <'l', 'g' or 'e'> CheckValue [further 'or' checks]

This is special. If this object check fails, it removes the object from
the actively tracked object list like a free() would do. E.g. Warzone
2100 uses the player ID in all objects. We don't want to make opponents
indestructible, so we check the player ID with this.

### dynmemend

Here, the memory class is closed. Static, dynamic or pointer memory can
follow.

## Dynamic Memory Adaption

### adapt_script RelScriptPath

A script can be run if requested by ugtrain (-A) to automatically adapt
the code address of a dynamic object in the config. Compilers and their
options differ from distribution to distribution. Also the game version
may differ. But if the object size is still the same, then there is a
good chance to adapt by searching for the allocation in the disassembly.
The path is relative to the config path.

### adapt_required 1

This is meant to ensure that the user is asked if he wants the automatic
adaption to be run when he uses this config the first time. This is why
this option is likely set in examples. It can be set to '0' afterwards.

## Pointer Memory

ptrmemstart PtrMemName PtrMemSize

The size is only important for the dump and the name is used for the
pointer entry in a dynamic memory section.

### < Values + checks like in static memory but with PtrMemOffset as Addr >

### ValName OffsInPtrMem DataType [optional check] WishValue KeyBindings ActState
### ValName OffsInPtrMem DataType [optional check] watch

Further pointers in pointer memory (chaining) is not possible. The offset
in pointer memory is added to the value of the read pointer value.

### check OffsInPtrMem DataType <'l', 'g' or 'e'> CheckValue [further 'or' checks]

### ptrmemend

Here, the pointer memory is closed. Static, dynamic or pointer memory can
follow.

## Additional Info

Dynamic memory always needs preloading (-P option). Ugtrain runs the game
as regular user with it. Here, only the code address in the binary where
"malloc" is called remains static. We have to find it and its location on
the stack with information from inside the game process.
See doc/ugtrain-dynmem.txt how to discover and adapt it to other versions.

### $ ugtrain -P [lib] \<config\>

Ugtrain detects a position independent executable (PIE) automatically from
its load address and converts code addresses between in-binary and
in-memory.
