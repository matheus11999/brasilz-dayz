// Zombie threat state enum - persistent state system matching base game architecture
enum EZombieThreatState
{
	IDLE = 0,           // Wandering, no threats detected
	INVESTIGATING = 1,  // Heard sound, investigating location
	CHASING = 2,        // Sees target, chasing
	ATTACKING = 3       // In melee range, attacking
};

