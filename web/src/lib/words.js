// 50 light, friendly English words used to seed a default nickname when
// the user opens hashhush for the first time. One word + two digits.

const WORDS = [
    'sleepy', 'sneaky', 'fluffy', 'grumpy', 'plucky',
    'cheery', 'wobbly', 'bouncy', 'dapper', 'frisky',
    'lucky', 'merry', 'snappy', 'spiffy', 'witty',
    'jazzy', 'breezy', 'goofy', 'jolly', 'nimble',
    'quirky', 'rascal', 'silly', 'snazzy', 'zesty',
    'penguin', 'otter', 'panda', 'narwhal', 'gecko',
    'puffin', 'wombat', 'badger', 'racoon', 'beaver',
    'capybara', 'hedgehog', 'octopus', 'platypus', 'meerkat',
    'mongoose', 'lemur', 'tapir', 'walrus', 'dolphin',
    'sparrow', 'pelican', 'flamingo', 'mantis', 'cricket'
];

export function randomNickname() {
    const word = WORDS[Math.floor(Math.random() * WORDS.length)];
    // 10-99 keeps the digit pair always two characters wide (no leading zero
    // padding that would feel ID-like).
    const suffix = 10 + Math.floor(Math.random() * 90);
    return `${word}${suffix}`;
}
