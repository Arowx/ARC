# arc_check runs every block (0.10.12, from Grand Token Acquisition)

## The hello sample says hello

```arc-check
game samples/hello/hello.c
do step 2
expect frame.texts[*].text contains "HELLO"
```

## This one fails on purpose

```arc-check
do step 2
expect frame.texts[*].text contains "Goodbye"
```

## It carries on from the block before (block 2 started a fresh game: 2 + 3 ticks)

```arc-check
continue
do step 3
expect tick == 5
```
