module rbtrend_atmosphere_pass_block;

// The atmosphere pass block (rbtrend_atmosphere_pass.slang) at set 0, as
// Rend's table passes read it: an atmosphere's (ATMOSPHERE_TABLES) or a
// scene's (LIT_ATMOSPHERE and its environment fill). Apart from the block's
// type so a pass with another set 0 (the shell pass, whose set 0 is the lit
// scene's) can read the block elsewhere.

import rbtrend_atmosphere_model;
import rbtrend_atmosphere_pass;

[[vk::binding(0, 0)]] public ConstantBuffer<RbtrendAtmospherePass> u_atmosphere : register(b0, space0);

// The medium the block holds.
public AtmosphereMedium pass_medium() {
    return block_medium(u_atmosphere);
}
