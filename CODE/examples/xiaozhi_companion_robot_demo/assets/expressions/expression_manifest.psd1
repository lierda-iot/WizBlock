@{
    Version = 1
    DefaultPackId = 'icebox'
    Packs = @(
        @{ PackId = 'icebox'; DisplayName = 'Icebox Cat'; Order = 0 }
        @{ PackId = 'crimson_slime'; DisplayName = 'Crimson Slime'; Order = 1 }
        @{ PackId = 'jade_frog'; DisplayName = 'Jade Frog'; Order = 2 }
        @{ PackId = 'bone_skull'; DisplayName = 'Bone Skull'; Order = 3 }
        @{ PackId = 'cobalt_owl'; DisplayName = 'Cobalt Owl'; Order = 4 }
        @{ PackId = 'magenta_octopus'; DisplayName = 'Magenta Octopus'; Order = 5 }
        @{ PackId = 'silver_husky'; DisplayName = 'Silver Husky'; Order = 6 }
        @{ PackId = 'amber_duck'; DisplayName = 'Amber Duck'; Order = 7 }
    )
    Scenes = @(
        @{ SceneId = 'idle'; Required = $true; Fallback = $null }
        @{ SceneId = 'blink'; Required = $true; Fallback = 'idle' }
        @{ SceneId = 'idle_sway_left_up'; Required = $true; Fallback = 'idle' }
        @{ SceneId = 'idle_sway_right_up'; Required = $true; Fallback = 'idle' }
        @{ SceneId = 'listen_focus'; Required = $true; Fallback = 'idle' }
        @{ SceneId = 'think'; Required = $true; Fallback = 'listen_focus' }
        @{ SceneId = 'turn_gaze_left'; Required = $true; Fallback = 'listen_focus' }
        @{ SceneId = 'turn_gaze_right'; Required = $true; Fallback = 'listen_focus' }
        @{ SceneId = 'talk_closed'; Required = $true; Fallback = 'idle' }
        @{ SceneId = 'talk_open'; Required = $true; Fallback = 'talk_closed' }
        @{ SceneId = 'touch_pout_compress'; Required = $true; Fallback = 'idle' }
        @{ SceneId = 'touch_pout_expand'; Required = $true; Fallback = 'touch_pout_compress' }
    )
}
