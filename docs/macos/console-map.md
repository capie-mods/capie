# macOS: mapa konsoli API 3

Stan: 2026-10-16. Analiza statyczna dokładnego pliku opisanego w `re-notes.md`.
VA poniżej są bez ASLR; RVA = VA − 0x100000000. Nie uruchomiono gry.
Źródło: istniejący dump llvm-objdump odczytany helperem `re/macho.py`; sesja REA
używana równolegle przez głównego agenta nie była modyfikowana.
Surowe bajty i pełny SHA-256 zapisano w `re/console-evidence.json`.

## Wynik

Konsola wymaga osobnej mapy: samo `game_layout.hpp` i pliki `*_pins.hpp` nie
obejmują wszystkiego. `console_layout.hpp` przechowuje dodatkowe adresy, a
`console_wiring.hpp` zawiera 13 sygnatur, 25 pinów instrukcji i 11 pinów celów.
`country_of`, `sprite_find`, global bazy sprite i offset cosmetic tag są wspólne
z `game_layout.hpp`; pozostałe pozycje konsolowe nie są tam kompletnie opisane.
W referencji Windows konsola jest tylko developer candidate, aktywowany przez
`build.ps1 -Console`; nie należy dodawać jej do domyślnego release przy okazji portu.

### Adresy potwierdzone statycznie

| Element Windows | macOS VA | Dowód / ograniczenie |
|---|---|---|
| singleton slot 0x35E1A80 | 0x1035e0610 | `102a67460 mov [rip+0xb791a9],rax`; następna instrukcja 0x102a67467. Destruktor zeruje ten sam slot. |
| create 0x24B4C40 | 0x102a673e0 | Alokacja 0xc0 bajtów, wektor, allocator i trzy predykaty, zapis singletonu. |
| add_or_replace 0x24B4920 | 0x102a676a0 | RDI manager, RSI źródłowy TempVector; liczba `[rsi+0xc]`; pętla stride 0x168 wywołuje 0x102a674b0. |
| pojedynczy add-or-replace | 0x102a674b0 | Porównanie nazw przez strcasecmp; znaleziony wpis kopiowany, brak wpisu tail-call do 0x102a69750. |
| lookup 0x24B54C0 | 0x102a67b50 | RDI manager, RSI libc++ string; nazwy i aliasy porównane przez strcasecmp, RAX entry/null. |
| dispatch 0x24B4D60 | 0x102a67c70 | RDI wynik, RSI manager, RDX token vector; kontrola trybu przed wyszukiwaniem i wywołaniem akcji. Wrapper 0x102a67c50 zwiększa manager+0xb4. |
| string_assign 0x129CA0 | 0x1007b3c50 | RDI string, RSI bytes, RDX size; SSO i heap, memmove/memcpy, terminator, RAX=self. |
| set_cosmetic_tag 0x282540 | 0x100166b30 | Rejestracja nazwy przy 0x1001a357f i wskaźnika akcji przy 0x1001a35f7; argc=2. |
| drop_cosmetic_tag 0x253220 | 0x100166bf0 | Rejestracja nazwy 0x1001a36ce i akcji 0x1001a3748; argc=1; dodaje pusty argument i wywołuje set action. |
| eval_effect 0x263690 | 0x10015e190 | Nazwa z immediates 0x1001a0571/582 daje `eval_effect`; action target `0x1001a05e5−0x42455`. Argumenty wejściowe RDI result, RSI argv; dalszej semantyki wykonania skryptu nie audytowano w całości. |
| country_of 0xBB4390 | 0x1011d7110 | Wywołanie z set action; odczyt int32 tagu, indeksowanie stanu gry +0x308 i +0x2d8. |
| country_set_cosmetic 0x711260 | 0x1011ca6a0 | Set action przekazuje RDI kraj, RSI drugi string; getter/porównanie/assign pola +0x1200, potem aktualizacja. |
| country.cosmetic_tag 0x1488 | +0x1200 | `1011ca75d lea r14,[rbx+0x1200]` przed libc++ assignment. |

### Różnica ABI parsera tagów

Windows `TagParseFn(out_tag, name)` nie wolno podmienić adresem 0x10012ca30:
macOS set action woła `0x10012ca30(string*) -> int32` (RDI wejście, EAX wynik),
zapisuje wynik na stosie i przekazuje wskaźnik do `0x1011d7110`.
Wewnątrz wrappera: kopia stringa, wywołanie `0x102ac0ea0`, następnie
`0x1011d6bd0(out_tag, string*)` i `0x1011d6bf0(tag*) -> int32`.
`0x1011d6bd0` zeruje out+0 i out+8, po czym skacze do 0x1011d6a50.
To silny kandydat odpowiednika konstruktora, ale pełny layout i normalizacja
przed konstrukcją wymagają osobnego dowodu. Adapter powinien odtwarzać sprawdzony
flow, nie zgadywać zgodności typów Windows.

### Layout potwierdzony instrukcjami

| Pole | macOS | Dowód |
|---|---|---|
| CommandEntry size | 0x168 | pętle 0x102a676b9/0x102a67bb4 |
| release flag | +0x00 | zapis 0x1001a3571, gate 0x102a67e2b |
| name | +0x08, libc++ 24 B | baza wpisu rbp−0x7bf0; nazwa rbp−0x7be8; lookup 0x102a67bbf |
| alias count / pointers | +0x20 / +0x28 | 0x102a67be0 / 0x102a67b84; trzy wskaźniki mieszczą się przed +0x40 |
| description | +0x40, libc++ 24 B | 0x102a67588/58c oraz konstrukcja rbp−0x7bb0 |
| action / drugi pointer | +0x58 / +0x60 | 0x102a67e6e/0x102a67f16; drugi pointer istnieje np. w komendzie effect, jego rola nie jest dowiedziona jako unused |
| context | +0x68 | 0x102a67f1a ładuje trzeci argument RDX |
| arg_name_count / strings | +0x70 / +0x78 | rejestracja set: rbp−0x7b80=2, rbp−0x7b78 pierwszy string; copy/destructor idą co 0x18, kończą +0x150 |
| TempVector data/capacity/count/allocator | +0/+8/+0xc/+0x10 | argument count 0x100166b53, dispatcher 0x102a67ec6/eca i lokalny allocator rbp−0x40 względem rbp−0x50 |
| TempVector size / string stride | 0x18 / 0x18 | lokalny wektor trzy qword, argv loop 0x102a67f05 |
| result flag / message / size | +0 / +8 / 0x20 | set action zeruje byte, 16 B przy +8 i 8 B przy +0x18; dispatcher przenosi te same pola |
| manager entries/count/allocator | +0/+0xc/+0x10 | lookup 0x102a67b61/73, create 0x102a673f1/fb |
| manager gate A/C/B | +0x40/+0xa0/+0x70 | 0x102a67c9a, 0x102a67cb7, 0x102a67cd1, wywołanie przez vtable+0x30 |

Akcja otrzymuje `RDI=result_storage`, `RSI=argv_without_command`, `RDX=context`
(0x102a67f1a–0x102a67f2a). Wynik ma zniszczalny string, więc prawdopodobnym
źródłowym typem jest zwrot obiektu przez hidden sret. Maszynowo set/drop zwracają
RAX=RDI. Przed deklaracją callbacka sprawdzić mały fixture x86_64 z tym samym
layoutem i porównać kod ABI; nie traktować C++ prototypu Windows jako dowodu.

## Braki i granice dowodu

- Nie ustalono w tym zakresie `sprite_find` i `sprite_database` dla macOS; są też
  wymaganiami portretów. Bez nich pełny console prepare musi odmawiać aktywacji.
- `0x100697780` zawiera rejestrację przy 0x100697a45; kandydat odpowiednika
  RestoreDeviceObjects, lecz tożsamość wymaga vtable/callgraph. Druga rejestracja
  występuje w 0x1007aad80 przy 0x1007ac2a7. Obie korzystają z potwierdzonego
  managera i add-vector. Nie zastępować Windows restoration pin samym podobieństwem.
- Gate zachowuje kolejność A, potem C, potem B, przed lookup. Powiązanie każdej
  lambdy z release/multiplayer/ironman należy domknąć przez RTTI wskazanych vtable;
  sam komunikat potwierdza semantykę całej bramki, nie etykiety każdej lambdy.
- Nie dowiedziono pełnego lifetime managera, wszystkich writers, zachowania po
  reloadzie i thread affinity; nie rejestrowano komend i nie testowano kolizji w grze.
- JSON poniżej dokumentuje bajty odczytane z tego binary. Nie stanowi gotowej tabeli
  runtime pinów: trzeba jeszcze maskowanie rel32/RIP, unikalność, wszystkie wymagane
  cele oraz test odmowy aktywacji dla każdej zmienionej lokalizacji.

## Wyciągi instrukcji

## 0x1001a356a..0x1001a3665

```asm
1001a356a:	lea	r13, [rbp - 0x7bf0]
1001a3571:	mov	byte ptr [rbp - 0x7bf0], 0x1
1001a3578:	mov	byte ptr [rbp - 0x7be8], 0x20
1001a357f:	movups	xmm0, xmmword ptr [rip + 0x2d8559a] ## literal pool for: "set_cosmetic_tag"
1001a3586:	movups	xmmword ptr [rbp - 0x7be7], xmm0
1001a358d:	mov	byte ptr [rbp - 0x7bd7], 0x0
1001a3594:	mov	dword ptr [rbp - 0x7bd0], 0x0
1001a359e:	mov	qword ptr [rbp - 0x7bc8], r12
1001a35a5:	movups	xmmword ptr [rbp - 0x7bc0], xmm1
1001a35ac:	mov	edi, 0x30
1001a35b1:	call	0x102a7fbd0
1001a35b6:	mov	qword ptr [rbp - 0x7ba0], rax
1001a35bd:	mov	qword ptr [rbp - 0x7bb0], 0x31
1001a35c8:	mov	qword ptr [rbp - 0x7ba8], 0x2b
1001a35d3:	movups	xmm0, xmmword ptr [rip + 0x2d85572] ## literal pool for: "pecified country"
1001a35da:	movups	xmmword ptr [rax + 0x1b], xmm0
1001a35de:	movups	xmm0, xmmword ptr [rip + 0x2d8555c] ## literal pool for: "ag' for a specified country"
1001a35e5:	movups	xmmword ptr [rax + 0x10], xmm0
1001a35e9:	movups	xmm0, xmmword ptr [rip + 0x2d85541] ## literal pool for: "Sets 'cosmetic tag' for a specified country"
1001a35f0:	movups	xmmword ptr [rax], xmm0
1001a35f3:	mov	byte ptr [rax + 0x2b], 0x0
1001a35f7:	lea	rax, [rip - 0x3cace]
1001a35fe:	mov	qword ptr [rbp - 0x7b98], rax
1001a3605:	xorps	xmm1, xmm1
1001a3608:	movups	xmmword ptr [rbp - 0x7b90], xmm1
1001a360f:	mov	dword ptr [rbp - 0x7b80], 0x2
1001a3619:	mov	byte ptr [rbp - 0x7b78], 0x1a
1001a3620:	mov	qword ptr [rbp - 0x7b77], r15
1001a3627:	mov	qword ptr [rbp - 0x7b72], r14
1001a362e:	mov	byte ptr [rbp - 0x7b6a], 0x0
1001a3635:	mov	byte ptr [rbp - 0x7b60], 0x1c
1001a363c:	movabs	rax, 0x6974656d736f633c
1001a3646:	mov	qword ptr [rbp - 0x7b5f], rax
1001a364d:	movabs	rax, 0x3e67617420636974
1001a3657:	mov	qword ptr [rbp - 0x7b59], rax
1001a365e:	mov	byte ptr [rbp - 0x7b51], 0x0
```

## 0x1001a36c0..0x1001a3786

```asm
1001a36c0:	mov	byte ptr [rbp - 0x7a88], 0x1
1001a36c7:	mov	byte ptr [rbp - 0x7a80], 0x22
1001a36ce:	movups	xmm0, xmmword ptr [rip + 0x2d854a5] ## literal pool for: "drop_cosmetic_tag"
1001a36d5:	movups	xmmword ptr [rbp - 0x7a7f], xmm0
1001a36dc:	mov	word ptr [rbp - 0x7a6f], 0x67
1001a36e5:	mov	dword ptr [rbp - 0x7a68], 0x0
1001a36ef:	mov	qword ptr [rbp - 0x7a60], r12
1001a36f6:	movups	xmmword ptr [rbp - 0x7a58], xmm1
1001a36fd:	mov	edi, 0x30
1001a3702:	call	0x102a7fbd0
1001a3707:	mov	qword ptr [rbp - 0x7a38], rax
1001a370e:	mov	qword ptr [rbp - 0x7a48], 0x31
1001a3719:	mov	qword ptr [rbp - 0x7a40], 0x2c
1001a3724:	movups	xmm0, xmmword ptr [rip + 0x2d8547d] ## literal pool for: "pecified country"
1001a372b:	movups	xmmword ptr [rax + 0x1c], xmm0
1001a372f:	movups	xmm0, xmmword ptr [rip + 0x2d85466] ## literal pool for: "tag' for a specified country"
1001a3736:	movups	xmmword ptr [rax + 0x10], xmm0
1001a373a:	movups	xmm0, xmmword ptr [rip + 0x2d8544b] ## literal pool for: "Drops 'cosmetic tag' for a specified country"
1001a3741:	movups	xmmword ptr [rax], xmm0
1001a3744:	mov	byte ptr [rax + 0x2c], 0x0
1001a3748:	lea	rax, [rip - 0x3cb5f]
1001a374f:	mov	qword ptr [rbp - 0x7a30], rax
1001a3756:	xorps	xmm0, xmm0
1001a3759:	movups	xmmword ptr [rbp - 0x7a28], xmm0
1001a3760:	mov	dword ptr [rbp - 0x7a18], 0x1
1001a376a:	mov	byte ptr [rbp - 0x7a10], 0x1a
1001a3771:	mov	qword ptr [rbp - 0x7a0f], r15
1001a3778:	mov	qword ptr [rbp - 0x7a0a], r14
1001a377f:	mov	byte ptr [rbp - 0x7a02], 0x0
```

## 0x1001a0563..0x1001a0604

```asm
1001a0563:	mov	byte ptr [rbp - 0xc678], 0x1
1001a056a:	mov	byte ptr [rbp - 0xc670], 0x16
1001a0571:	movabs	rax, 0x6666655f6c617665
1001a057b:	mov	qword ptr [rbp - 0xc66f], rax
1001a0582:	mov	dword ptr [rbp - 0xc668], 0x74636566
1001a058c:	mov	qword ptr [rbp - 0xc650], r12
1001a0593:	mov	edi, 0x30
1001a0598:	call	0x102a7fbd0
1001a059d:	mov	qword ptr [rbp - 0xc628], rax
1001a05a4:	mov	qword ptr [rbp - 0xc638], 0x31
1001a05af:	mov	qword ptr [rbp - 0xc630], 0x2b
1001a05ba:	movups	xmm0, xmmword ptr [rip + 0x2d8798f] ## literal pool for: "a selected scope"
1001a05c1:	movups	xmmword ptr [rax + 0x1b], xmm0
1001a05c5:	movups	xmm0, xmmword ptr [rip + 0x2d87979] ## literal pool for: " effect on a selected scope"
1001a05cc:	movups	xmmword ptr [rax + 0x10], xmm0
1001a05d0:	movups	xmm0, xmmword ptr [rip + 0x2d8795e] ## literal pool for: "Runs the inlined effect on a selected scope"
1001a05d7:	movups	xmmword ptr [rax], xmm0
1001a05da:	mov	byte ptr [rax + 0x2b], 0x0
1001a05de:	lea	rax, [rip - 0x42455]
1001a05e5:	mov	qword ptr [rbp - 0xc620], rax
1001a05ec:	lea	rdi, [rbp - 0xc600]
1001a05f3:	lea	r13, [rbp - 0xc510]
1001a05fa:	mov	esi, 0x258
1001a05ff:	call	0x102b25f4a ## symbol stub for: ___bzero
```

## 0x100166b30..0x100166bc3

```asm
100166b30:	push	rbp
100166b31:	mov	rbp, rsp
100166b34:	push	r15
100166b36:	push	r14
100166b38:	push	rbx
100166b39:	push	rax
100166b3a:	mov	rbx, rdi
100166b3d:	mov	byte ptr [rdi], 0x0
100166b40:	lea	r14, [rdi + 0x8]
100166b44:	xorps	xmm0, xmm0
100166b47:	movups	xmmword ptr [rdi + 0x8], xmm0
100166b4b:	mov	qword ptr [rdi + 0x18], 0x0
100166b53:	cmp	dword ptr [rsi + 0xc], 0x2
100166b57:	jne	0x100166b88
100166b59:	mov	r15, rsi
100166b5c:	mov	rdi, qword ptr [rsi]
100166b5f:	call	0x10012ca30
100166b64:	mov	dword ptr [rbp - 0x20], eax
100166b67:	test	eax, eax
100166b69:	jle	0x100166b9e
100166b6b:	mov	r15, qword ptr [r15]
100166b6e:	lea	rdi, [rbp - 0x20]
100166b72:	call	0x1011d7110
100166b77:	add	r15, 0x18
100166b7b:	mov	rdi, rax
100166b7e:	mov	rsi, r15
100166b81:	call	0x1011ca6a0
100166b86:	jmp	0x100166bb5
100166b88:	lea	rsi, [rip + 0x2dba600] ## literal pool for: "Invalid number of arguments"
100166b8f:	mov	edx, 0x1b
100166b94:	mov	rdi, r14
100166b97:	call	0x1007b3c50
100166b9c:	jmp	0x100166bb2
100166b9e:	lea	rsi, [rip + 0x2dba5ce] ## literal pool for: "Invalid tag for the command"
100166ba5:	mov	edx, 0x1b
100166baa:	mov	rdi, r14
100166bad:	call	0x1007b3c50
100166bb2:	mov	byte ptr [rbx], 0x0
100166bb5:	mov	rax, rbx
100166bb8:	add	rsp, 0x8
100166bbc:	pop	rbx
100166bbd:	pop	r14
100166bbf:	pop	r15
100166bc1:	pop	rbp
100166bc2:	ret
```

## 0x100166bf0..0x100166c24

```asm
100166bf0:	push	rbp
100166bf1:	mov	rbp, rsp
100166bf4:	push	r15
100166bf6:	push	r14
100166bf8:	push	r13
100166bfa:	push	r12
100166bfc:	push	rbx
100166bfd:	sub	rsp, 0x38
100166c01:	mov	rbx, rdi
100166c04:	mov	byte ptr [rdi], 0x0
100166c07:	lea	r14, [rdi + 0x8]
100166c0b:	xorps	xmm0, xmm0
100166c0e:	movups	xmmword ptr [rdi + 0x8], xmm0
100166c12:	mov	qword ptr [rdi + 0x18], 0x0
100166c1a:	cmp	dword ptr [rsi + 0xc], 0x1
100166c1e:	jne	0x100166cf3
```

## 0x100166d49..0x100166d6c

```asm
100166d49:	inc	r13d
100166d4c:	mov	dword ptr [rbp - 0x34], r13d
100166d50:	lea	rdi, [rbp - 0x60]
100166d54:	lea	rsi, [rbp - 0x40]
100166d58:	call	0x100166b30
100166d5d:	test	byte ptr [rbp - 0x58], 0x1
100166d61:	je	0x100166d6c
100166d63:	mov	rdi, qword ptr [rbp - 0x48]
100166d67:	call	0x102a7fc10
```

## 0x102a673e0..0x102a67469

```asm
102a673e0:	push	rbp
102a673e1:	mov	rbp, rsp
102a673e4:	mov	edi, 0xc0
102a673e9:	call	0x102a7fbd0
102a673ee:	xorps	xmm0, xmm0
102a673f1:	movups	xmmword ptr [rax], xmm0
102a673f4:	mov	rcx, qword ptr [rip + 0x8355cd] ## literal pool symbol address: __ZZ24PdxGetNewDeleteAllocatorvE5Alloc
102a673fb:	mov	qword ptr [rax + 0x10], rcx
102a673ff:	lea	rcx, [rax + 0x20]
102a67403:	mov	rdx, qword ptr [rip + 0x8304a6]
102a6740a:	add	rdx, 0x10
102a6740e:	mov	qword ptr [rax + 0x20], rdx
102a67412:	mov	qword ptr [rax + 0x40], rcx
102a67416:	lea	rcx, [rax + 0x50]
102a6741a:	mov	rdx, qword ptr [rip + 0x830497]
102a67421:	add	rdx, 0x10
102a67425:	mov	qword ptr [rax + 0x50], rdx
102a67429:	mov	qword ptr [rax + 0x70], rcx
102a6742d:	lea	rcx, [rax + 0x80]
102a67434:	mov	rdx, qword ptr [rip + 0x83046d]
102a6743b:	add	rdx, 0x10
102a6743f:	mov	qword ptr [rax + 0x80], rdx
102a67446:	mov	qword ptr [rax + 0xa0], rcx
102a6744d:	mov	word ptr [rax + 0xb0], 0x0
102a67456:	mov	dword ptr [rax + 0xb4], 0x0
102a67460:	mov	qword ptr [rip + 0xb791a9], rax
102a67467:	pop	rbp
102a67468:	ret
```

## 0x102a674b0..0x102a675c0

```asm
102a674b0:	push	rbp
102a674b1:	mov	rbp, rsp
102a674b4:	push	r15
102a674b6:	push	r14
102a674b8:	push	r13
102a674ba:	push	r12
102a674bc:	push	rbx
102a674bd:	sub	rsp, 0x18
102a674c1:	mov	rbx, rsi
102a674c4:	mov	qword ptr [rbp - 0x30], rdi
102a674c8:	mov	eax, dword ptr [rdi + 0xc]
102a674cb:	test	eax, eax
102a674cd:	jle	0x102a6753d
102a674cf:	test	byte ptr [rbx + 0x8], 0x1
102a674d3:	je	0x102a674db
102a674d5:	mov	r15, qword ptr [rbx + 0x18]
102a674d9:	jmp	0x102a674df
102a674db:	lea	r15, [rbx + 0x9]
102a674df:	lea	rcx, [rbx + 0x8]
102a674e3:	mov	qword ptr [rbp - 0x38], rcx
102a674e7:	mov	rcx, qword ptr [rbp - 0x30]
102a674eb:	mov	r13, qword ptr [rcx]
102a674ee:	imul	r14, rax, 0x168
102a674f5:	xor	r12d, r12d
102a674f8:	nop	dword ptr [rax + rax]
102a67500:	test	byte ptr [r13 + r12 + 0x8], 0x1
102a67506:	jne	0x102a67520
102a67508:	lea	rsi, [r12 + r13]
102a6750c:	add	rsi, 0x9
102a67510:	jmp	0x102a67525
102a67512:	nop	word ptr cs:[rax + rax]
102a67520:	mov	rsi, qword ptr [r13 + r12 + 0x18]
102a67525:	mov	rdi, r15
102a67528:	call	0x102b265ce ## symbol stub for: _strcasecmp
102a6752d:	test	eax, eax
102a6752f:	je	0x102a67557
102a67531:	add	r12, 0x168
102a67538:	cmp	r14, r12
102a6753b:	jne	0x102a67500
102a6753d:	mov	rdi, qword ptr [rbp - 0x30]
102a67541:	mov	rsi, rbx
102a67544:	add	rsp, 0x18
102a67548:	pop	rbx
102a67549:	pop	r12
102a6754b:	pop	r13
102a6754d:	pop	r14
102a6754f:	pop	r15
102a67551:	pop	rbp
102a67552:	jmp	0x102a69750
102a67557:	movzx	eax, byte ptr [rbx]
102a6755a:	mov	byte ptr [r13 + r12], al
102a6755f:	lea	rdi, [r12 + r13]
102a67563:	add	rdi, 0x8
102a67567:	mov	rsi, qword ptr [rbp - 0x38]
102a6756b:	call	0x102b25d22 ## symbol stub for: __ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEaSERKS5_
102a67570:	movups	xmm0, xmmword ptr [rbx + 0x20]
102a67574:	movups	xmm1, xmmword ptr [rbx + 0x30]
102a67578:	movups	xmmword ptr [r13 + r12 + 0x20], xmm0
102a6757e:	movups	xmmword ptr [r13 + r12 + 0x30], xmm1
102a67584:	lea	rdi, [r12 + r13]
102a67588:	add	rdi, 0x40
102a6758c:	lea	rsi, [rbx + 0x40]
102a67590:	call	0x102b25d22 ## symbol stub for: __ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEaSERKS5_
102a67595:	movups	xmm0, xmmword ptr [rbx + 0x58]
102a67599:	movups	xmm1, xmmword ptr [rbx + 0x64]
102a6759d:	movups	xmmword ptr [r13 + r12 + 0x58], xmm0
102a675a3:	movups	xmmword ptr [r13 + r12 + 0x64], xmm1
102a675a9:	lea	rdi, [r12 + r13]
102a675ad:	add	rdi, 0x78
102a675b1:	lea	rsi, [rbx + 0x78]
102a675b5:	call	0x102b25d22 ## symbol stub for: __ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEaSERKS5_
102a675ba:	lea	rdi, [r12 + r13]
102a675be:	add	rdi, 0x90
```

## 0x102a676a0..0x102a676e6

```asm
102a676a0:	push	rbp
102a676a1:	mov	rbp, rsp
102a676a4:	push	r15
102a676a6:	push	r14
102a676a8:	push	rbx
102a676a9:	push	rax
102a676aa:	movsxd	rax, dword ptr [rsi + 0xc]
102a676ae:	test	rax, rax
102a676b1:	je	0x102a676db
102a676b3:	mov	rbx, rdi
102a676b6:	mov	r14, qword ptr [rsi]
102a676b9:	imul	r15, rax, 0x168
102a676c0:	mov	rdi, rbx
102a676c3:	mov	rsi, r14
102a676c6:	call	0x102a674b0
102a676cb:	add	r14, 0x168
102a676d2:	add	r15, -0x168
102a676d9:	jne	0x102a676c0
102a676db:	add	rsp, 0x8
102a676df:	pop	rbx
102a676e0:	pop	r14
102a676e2:	pop	r15
102a676e4:	pop	rbp
102a676e5:	ret
```

## 0x102a67b50..0x102a67c43

```asm
102a67b50:	push	rbp
102a67b51:	mov	rbp, rsp
102a67b54:	push	r15
102a67b56:	push	r14
102a67b58:	push	r13
102a67b5a:	push	r12
102a67b5c:	push	rbx
102a67b5d:	sub	rsp, 0x18
102a67b61:	mov	eax, dword ptr [rdi + 0xc]
102a67b64:	mov	qword ptr [rbp - 0x38], rax
102a67b68:	test	eax, eax
102a67b6a:	jle	0x102a67c20
102a67b70:	mov	rbx, rsi
102a67b73:	mov	r15, qword ptr [rdi]
102a67b76:	test	byte ptr [rsi], 0x1
102a67b79:	je	0x102a67b81
102a67b7b:	mov	rbx, qword ptr [rbx + 0x10]
102a67b7f:	jmp	0x102a67b84
102a67b81:	inc	rbx
102a67b84:	lea	r14, [r15 + 0x28]
102a67b88:	xor	r13d, r13d
102a67b8b:	mov	qword ptr [rbp - 0x30], r15
102a67b8f:	jmp	0x102a67bb4
102a67b91:	nop	word ptr cs:[rax + rax]
102a67ba0:	inc	r13
102a67ba3:	add	r14, 0x168
102a67baa:	cmp	r13, qword ptr [rbp - 0x38]
102a67bae:	mov	r15, qword ptr [rbp - 0x30]
102a67bb2:	je	0x102a67c20
102a67bb4:	imul	r12, r13, 0x168
102a67bbb:	lea	rdi, [r15 + r12]
102a67bbf:	add	rdi, 0x8
102a67bc3:	test	byte ptr [r15 + r12 + 0x8], 0x1
102a67bc9:	jne	0x102a67bd0
102a67bcb:	inc	rdi
102a67bce:	jmp	0x102a67bd4
102a67bd0:	mov	rdi, qword ptr [rdi + 0x10]
102a67bd4:	mov	rsi, rbx
102a67bd7:	call	0x102b265ce ## symbol stub for: _strcasecmp
102a67bdc:	test	eax, eax
102a67bde:	je	0x102a67c31
102a67be0:	mov	r12d, dword ptr [r15 + r12 + 0x20]
102a67be5:	test	r12d, r12d
102a67be8:	jle	0x102a67ba0
102a67bea:	xor	r15d, r15d
102a67bed:	nop	dword ptr [rax]
102a67bf0:	mov	rdi, qword ptr [r14 + 8*r15]
102a67bf4:	mov	rsi, rbx
102a67bf7:	call	0x102b265ce ## symbol stub for: _strcasecmp
102a67bfc:	test	eax, eax
102a67bfe:	je	0x102a67c0a
102a67c00:	inc	r15
102a67c03:	cmp	r12, r15
102a67c06:	jne	0x102a67bf0
102a67c08:	jmp	0x102a67ba0
102a67c0a:	mov	eax, r13d
102a67c0d:	imul	rax, rax, 0x168
102a67c14:	mov	rcx, qword ptr [rbp - 0x30]
102a67c18:	add	rcx, rax
102a67c1b:	mov	rax, rcx
102a67c1e:	jmp	0x102a67c22
102a67c20:	xor	eax, eax
102a67c22:	add	rsp, 0x18
102a67c26:	pop	rbx
102a67c27:	pop	r12
102a67c29:	pop	r13
102a67c2b:	pop	r14
102a67c2d:	pop	r15
102a67c2f:	pop	rbp
102a67c30:	ret
102a67c31:	mov	eax, r13d
102a67c34:	imul	rax, rax, 0x168
102a67c3b:	add	r15, rax
102a67c3e:	mov	rax, r15
102a67c41:	jmp	0x102a67c22
```

## 0x102a67c70..0x102a67d06

```asm
102a67c70:	push	rbp
102a67c71:	mov	rbp, rsp
102a67c74:	push	r15
102a67c76:	push	r14
102a67c78:	push	r13
102a67c7a:	push	r12
102a67c7c:	push	rbx
102a67c7d:	sub	rsp, 0x78
102a67c81:	mov	r12, rdi
102a67c84:	mov	byte ptr [rdi], 0x0
102a67c87:	lea	r15, [rdi + 0x8]
102a67c8b:	xorps	xmm0, xmm0
102a67c8e:	movups	xmmword ptr [rdi + 0x8], xmm0
102a67c92:	mov	qword ptr [rdi + 0x18], 0x0
102a67c9a:	mov	rdi, qword ptr [rsi + 0x40]
102a67c9e:	test	rdi, rdi
102a67ca1:	je	0x102a681c6
102a67ca7:	mov	r14, rdx
102a67caa:	mov	rbx, rsi
102a67cad:	mov	rax, qword ptr [rdi]
102a67cb0:	call	qword ptr [rax + 0x30]
102a67cb3:	test	al, al
102a67cb5:	je	0x102a67d06
102a67cb7:	mov	rdi, qword ptr [rbx + 0xa0]
102a67cbe:	test	rdi, rdi
102a67cc1:	je	0x102a681c6
102a67cc7:	mov	rax, qword ptr [rdi]
102a67cca:	call	qword ptr [rax + 0x30]
102a67ccd:	test	al, al
102a67ccf:	jne	0x102a67ce8
102a67cd1:	mov	rdi, qword ptr [rbx + 0x70]
102a67cd5:	test	rdi, rdi
102a67cd8:	je	0x102a681c6
102a67cde:	mov	rax, qword ptr [rdi]
102a67ce1:	call	qword ptr [rax + 0x30]
102a67ce4:	test	al, al
102a67ce6:	je	0x102a67d06
102a67ce8:	mov	byte ptr [r12], 0x0
102a67ced:	lea	rsi, [rip + 0x7580aa] ## literal pool for: "Console not available in multiplayer or ironman mode."
102a67cf4:	mov	edx, 0x35
102a67cf9:	mov	rdi, r15
102a67cfc:	call	0x1007b3c50
102a67d01:	jmp	0x102a681b4
```

## 0x102a67e6e..0x102a67f62

```asm
102a67e6e:	cmp	qword ptr [rax + r13 + 0x58], 0x0
102a67e74:	je	0x102a68160
102a67e7a:	mov	byte ptr [rbx + 0xb1], 0x1
102a67e81:	xorps	xmm0, xmm0
102a67e84:	movaps	xmmword ptr [rbp - 0x50], xmm0
102a67e88:	mov	r14, qword ptr [rip + 0x834b39] ## literal pool symbol address: __ZZ24PdxGetNewDeleteAllocatorvE5Alloc
102a67e8f:	mov	qword ptr [rbp - 0x40], r14
102a67e93:	mov	ebx, dword ptr [r15 + 0xc]
102a67e97:	cmp	ebx, 0x2
102a67e9a:	jl	0x102a67f0e
102a67e9c:	dec	ebx
102a67e9e:	lea	rax, [8*rbx]
102a67ea6:	lea	rsi, [rax + 2*rax]
102a67eaa:	mov	rax, qword ptr [r14]
102a67ead:	mov	rdi, r14
102a67eb0:	mov	edx, 0x8
102a67eb5:	call	qword ptr [rax + 0x10]
102a67eb8:	mov	r12, rax
102a67ebb:	mov	rax, qword ptr [r14]
102a67ebe:	mov	rdi, r14
102a67ec1:	xor	esi, esi
102a67ec3:	call	qword ptr [rax + 0x18]
102a67ec6:	mov	qword ptr [rbp - 0x50], r12
102a67eca:	mov	dword ptr [rbp - 0x48], ebx
102a67ecd:	cmp	dword ptr [r15 + 0xc], 0x2
102a67ed2:	jl	0x102a67f0e
102a67ed4:	mov	ebx, 0x1
102a67ed9:	mov	r12d, 0x18
102a67edf:	lea	r14, [rbp - 0x50]
102a67ee3:	nop	word ptr cs:[rax + rax]
102a67ef0:	mov	rsi, qword ptr [r15]
102a67ef3:	add	rsi, r12
102a67ef6:	mov	rdi, r14
102a67ef9:	call	0x1025368a0
102a67efe:	inc	rbx
102a67f01:	movsxd	rax, dword ptr [r15 + 0xc]
102a67f05:	add	r12, 0x18
102a67f09:	cmp	rbx, rax
102a67f0c:	jl	0x102a67ef0
102a67f0e:	mov	rcx, qword ptr [rbp - 0x58]
102a67f12:	lea	rax, [rcx + r13]
102a67f16:	add	rax, 0x58
102a67f1a:	mov	rdx, qword ptr [rcx + r13 + 0x68]
102a67f1f:	lea	rdi, [rbp - 0xa0]
102a67f26:	lea	rsi, [rbp - 0x50]
102a67f2a:	call	qword ptr [rax]
102a67f2c:	movzx	eax, byte ptr [rbp - 0xa0]
102a67f33:	mov	r12, qword ptr [rbp - 0x60]
102a67f37:	mov	byte ptr [r12], al
102a67f3b:	mov	rsi, qword ptr [rbp - 0x50]
102a67f3f:	mov	rcx, qword ptr [rbp - 0x88]
102a67f46:	mov	rdx, qword ptr [rbp - 0x30]
102a67f4a:	mov	qword ptr [rdx + 0x10], rcx
102a67f4e:	movups	xmm0, xmmword ptr [rbp - 0x98]
102a67f55:	movups	xmmword ptr [rdx], xmm0
102a67f58:	mov	rcx, qword ptr [rbp - 0x68]
102a67f5c:	mov	byte ptr [rcx + 0xb0], al
```

## 0x1007b3c50..0x1007b3d92

```asm
1007b3c50:	push	rbp
1007b3c51:	mov	rbp, rsp
1007b3c54:	push	r15
1007b3c56:	push	r14
1007b3c58:	push	r13
1007b3c5a:	push	r12
1007b3c5c:	push	rbx
1007b3c5d:	sub	rsp, 0x18
1007b3c61:	mov	r14, rdx
1007b3c64:	mov	rbx, rdi
1007b3c67:	test	byte ptr [rdi], 0x1
1007b3c6a:	jne	0x1007b3c98
1007b3c6c:	cmp	r14, 0x16
1007b3c70:	jbe	0x1007b3d4a
1007b3c76:	lea	rax, [r14 + 0x11]
1007b3c7a:	cmp	rax, 0x27
1007b3c7e:	jb	0x1007b3d8a
1007b3c84:	mov	qword ptr [rbp - 0x38], rsi
1007b3c88:	lea	rax, [rbx + 0x1]
1007b3c8c:	mov	qword ptr [rbp - 0x30], rax
1007b3c90:	mov	r12d, 0x16
1007b3c96:	jmp	0x1007b3ce7
1007b3c98:	mov	rax, qword ptr [rbx]
1007b3c9b:	and	rax, -0x2
1007b3c9f:	lea	r12, [rax - 0x1]
1007b3ca3:	cmp	r12, r14
1007b3ca6:	jae	0x1007b3d50
1007b3cac:	mov	rcx, r14
1007b3caf:	sub	rcx, r12
1007b3cb2:	mov	r13, -0x11
1007b3cb9:	mov	rdx, -0x11
1007b3cc0:	sub	rdx, rax
1007b3cc3:	cmp	rdx, rcx
1007b3cc6:	jb	0x1007b3d8a
1007b3ccc:	mov	qword ptr [rbp - 0x38], rsi
1007b3cd0:	mov	rax, qword ptr [rbx + 0x10]
1007b3cd4:	mov	qword ptr [rbp - 0x30], rax
1007b3cd8:	movabs	rax, 0x7fffffffffffffe6
1007b3ce2:	cmp	r12, rax
1007b3ce5:	ja	0x1007b3d0a
1007b3ce7:	lea	rax, [r12 + r12]
1007b3ceb:	cmp	r14, rax
1007b3cee:	cmova	rax, r14
1007b3cf2:	mov	rcx, rax
1007b3cf5:	or	rcx, 0xf
1007b3cf9:	inc	rcx
1007b3cfc:	cmp	rax, 0x17
1007b3d00:	mov	r13d, 0x17
1007b3d06:	cmovae	r13, rcx
1007b3d0a:	mov	rdi, r13
1007b3d0d:	call	0x102a7fbd0
1007b3d12:	mov	r15, rax
1007b3d15:	mov	rdi, rax
1007b3d18:	mov	rsi, qword ptr [rbp - 0x38]
1007b3d1c:	mov	rdx, r14
1007b3d1f:	call	0x102b263b8 ## symbol stub for: _memcpy
1007b3d24:	cmp	r12, 0x16
1007b3d28:	je	0x1007b3d33
1007b3d2a:	mov	rdi, qword ptr [rbp - 0x30]
1007b3d2e:	call	0x102a7fc10
1007b3d33:	mov	r12, r15
1007b3d36:	add	r12, r14
1007b3d39:	mov	qword ptr [rbx + 0x10], r15
1007b3d3d:	or	r13, 0x1
1007b3d41:	mov	qword ptr [rbx], r13
1007b3d44:	mov	qword ptr [rbx + 0x8], r14
1007b3d48:	jmp	0x1007b3d73
1007b3d4a:	lea	r12, [rbx + 0x1]
1007b3d4e:	jmp	0x1007b3d54
1007b3d50:	mov	r12, qword ptr [rbx + 0x10]
1007b3d54:	mov	rdi, r12
1007b3d57:	mov	rdx, r14
1007b3d5a:	call	0x102b263be ## symbol stub for: _memmove
1007b3d5f:	test	byte ptr [rbx], 0x1
1007b3d62:	jne	0x1007b3d6c
1007b3d64:	lea	eax, [r14 + r14]
1007b3d68:	mov	byte ptr [rbx], al
1007b3d6a:	jmp	0x1007b3d70
1007b3d6c:	mov	qword ptr [rbx + 0x8], r14
1007b3d70:	add	r12, r14
1007b3d73:	mov	byte ptr [r12], 0x0
1007b3d78:	mov	rax, rbx
1007b3d7b:	add	rsp, 0x18
1007b3d7f:	pop	rbx
1007b3d80:	pop	r12
1007b3d82:	pop	r13
1007b3d84:	pop	r14
1007b3d86:	pop	r15
1007b3d88:	pop	rbp
1007b3d89:	ret
1007b3d8a:	mov	rdi, rbx
1007b3d8d:	call	0x1007b3bc0
```

## 0x10012caa4..0x10012cad6

```asm
10012caa4:	lea	rdi, [rbp - 0x30]
10012caa8:	call	0x102ac0ea0
10012caad:	lea	rdi, [rbp - 0x40]
10012cab1:	lea	rsi, [rbp - 0x30]
10012cab5:	call	0x1011d6bd0
10012caba:	lea	rdi, [rbp - 0x40]
10012cabe:	call	0x1011d6bf0
10012cac3:	mov	ebx, eax
10012cac5:	test	byte ptr [rbp - 0x30], 0x1
10012cac9:	je	0x10012cad4
10012cacb:	mov	rdi, qword ptr [rbp - 0x20]
10012cacf:	call	0x102a7fc10
10012cad4:	mov	eax, ebx
```

## 0x1011d6bd0..0x1011d6be8

```asm
1011d6bd0:	push	rbp
1011d6bd1:	mov	rbp, rsp
1011d6bd4:	mov	dword ptr [rdi], 0x0
1011d6bda:	mov	qword ptr [rdi + 0x8], 0x0
1011d6be2:	pop	rbp
1011d6be3:	jmp	0x1011d6a50
```

## 0x1011d7239..0x1011d726b

```asm
1011d7239:	mov	rax, qword ptr [r13]
1011d723d:	movsxd	rcx, dword ptr [rbx]
1011d7240:	mov	rax, qword ptr [rax + 0x308]
1011d7247:	mov	eax, dword ptr [rax + 4*rcx]
1011d724a:	cdqe
1011d724c:	jmp	0x1011d7250
1011d724e:	xor	eax, eax
1011d7250:	mov	rcx, qword ptr [r12 + 0x2d8]
1011d7258:	mov	rax, qword ptr [rcx + 8*rax]
1011d725c:	add	rsp, 0x8
1011d7260:	pop	rbx
1011d7261:	pop	r12
1011d7263:	pop	r13
1011d7265:	pop	r14
1011d7267:	pop	r15
1011d7269:	pop	rbp
1011d726a:	ret
```

## 0x1011ca6a0..0x1011ca77e

```asm
1011ca6a0:	push	rbp
1011ca6a1:	mov	rbp, rsp
1011ca6a4:	push	r15
1011ca6a6:	push	r14
1011ca6a8:	push	r12
1011ca6aa:	push	rbx
1011ca6ab:	sub	rsp, 0x10
1011ca6af:	mov	r15, rsi
1011ca6b2:	mov	rbx, rdi
1011ca6b5:	movzx	ecx, byte ptr [rdi + 0x1200]
1011ca6bc:	mov	rax, rcx
1011ca6bf:	shr	rax
1011ca6c2:	mov	rdx, rax
1011ca6c5:	test	cl, 0x1
1011ca6c8:	je	0x1011ca6d1
1011ca6ca:	mov	rdx, qword ptr [rbx + 0x1208]
1011ca6d1:	movzx	esi, byte ptr [r15]
1011ca6d5:	test	sil, 0x1
1011ca6d9:	je	0x1011ca6e6
1011ca6db:	mov	rdi, qword ptr [r15 + 0x8]
1011ca6df:	cmp	rdx, rdi
1011ca6e2:	je	0x1011ca6f1
1011ca6e4:	jmp	0x1011ca75d
1011ca6e6:	mov	rdi, rsi
1011ca6e9:	shr	rdi
1011ca6ec:	cmp	rdx, rdi
1011ca6ef:	jne	0x1011ca75d
1011ca6f1:	test	sil, 0x1
1011ca6f5:	je	0x1011ca6fd
1011ca6f7:	mov	rsi, qword ptr [r15 + 0x10]
1011ca6fb:	jmp	0x1011ca701
1011ca6fd:	lea	rsi, [r15 + 0x1]
1011ca701:	lea	r8, [rbx + 0x1201]
1011ca708:	mov	rdi, r8
1011ca70b:	test	cl, 0x1
1011ca70e:	je	0x1011ca717
1011ca710:	mov	rdi, qword ptr [rbx + 0x1210]
1011ca717:	jne	0x1011ca747
1011ca719:	test	rdx, rdx
1011ca71c:	je	0x1011ca880
1011ca722:	xor	ecx, ecx
1011ca724:	nop	word ptr cs:[rax + rax]
1011ca730:	movzx	edx, byte ptr [r8 + rcx]
1011ca735:	cmp	dl, byte ptr [rsi + rcx]
1011ca738:	jne	0x1011ca75d
1011ca73a:	inc	rcx
1011ca73d:	cmp	rax, rcx
1011ca740:	jne	0x1011ca730
1011ca742:	jmp	0x1011ca880
1011ca747:	test	rdx, rdx
1011ca74a:	je	0x1011ca880
1011ca750:	call	0x102b263b2 ## symbol stub for: _memcmp
1011ca755:	test	eax, eax
1011ca757:	je	0x1011ca880
1011ca75d:	lea	r14, [rbx + 0x1200]
1011ca764:	mov	rdi, r14
1011ca767:	mov	rsi, r15
1011ca76a:	call	0x102b25d22 ## symbol stub for: __ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEaSERKS5_
1011ca76f:	mov	rdi, rbx
1011ca772:	call	0x10117a790
1011ca777:	lea	r12, [rip + 0x2415eeb]
```

## 0x100697a2a..0x100697a4a

```asm
100697a2a:	mov	r14, qword ptr [r15]
100697a2d:	test	r14, r14
100697a30:	jne	0x100697a3a
100697a32:	call	0x102a673e0
100697a37:	mov	r14, qword ptr [r15]
100697a3a:	call	0x10012dd10
100697a3f:	mov	rdi, r14
100697a42:	mov	rsi, rax
100697a45:	call	0x102a676a0
```
