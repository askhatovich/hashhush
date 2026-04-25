export const en = {
    home: {
        subtitle: 'Ephemeral end-to-end encrypted group chat. No accounts. No tracking.',
        room_name: 'Room name',
        room_name_placeholder: 'Secret chat',
        nickname: 'Nickname',
        create: 'Create room',
        creating: 'Creating room…',
        privacy_note: 'The encryption key lives in the URL fragment (#) and is never sent to the server.',
        info_max: 'Up to {count} participants per room.',
        info_ttl: 'Rooms are deleted automatically after {duration} of inactivity.',
        info_cache: 'The server cannot read encrypted messages and keeps only the last {count} of them to restore context for new participants.',
        // Singular / plural pairs for the {duration} placeholder.
        unit_day_one: 'day',     unit_day_many: 'days',
        unit_hour_one: 'hour',   unit_hour_many: 'hours',
        unit_minute_one: 'minute', unit_minute_many: 'minutes'
    },
    chat: {
        connecting: 'Connecting…',
        joining: 'Joining…',
        deriving_key: 'Deriving key…',
        delete: 'Delete room',
        copy_link: 'Copy invite link',
        copied: 'Link copied',
        members: 'Members',
        you: 'you',
        send_placeholder: 'Type a message and press Enter',
        room_deleted: 'This room was deleted.',
        back_home: 'Back to home',
        connection_lost: 'Connection lost. Reconnecting…',
        sys_joined: '{nick} joined the chat',
        sys_left: '{nick} left the chat',
        unknown_peer: 'someone'
    },
    delete_modal: {
        title: 'Delete room?',
        body: 'All information about the room will be removed for everyone. This cannot be undone.',
        slide_to_confirm: 'Slide to confirm',
        cancel: 'Cancel'
    },
    errors: {
        room_not_found: 'This room no longer exists.',
        invalid_key: 'Invalid key. The link may be corrupted.',
        invalid_token: 'Invalid key. The link may be corrupted.',
        challenges_exhausted: 'This room reached its connection budget and no longer accepts new participants.',
        room_full: 'The room is full.',
        rooms_capacity_reached: 'The server is at capacity. Please try again later.',
        bad_json: 'Bad request.',
        internal: 'Server error. Please retry.',
        network: 'Network error.',
        not_joined: 'Not authorised for this room.',
        no_challenge: 'Authentication state lost. Please retry.'
    }
};
